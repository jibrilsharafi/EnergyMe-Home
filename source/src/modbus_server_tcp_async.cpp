// =================================================================================================
// eModbus: Copyright 2020 by Michael Harwerth, Bert Melis and the contributors to eModbus
//               MIT license - see license.md for details
// =================================================================================================

// Vendored from eModbus v1.7.6.stable (src/ModbusServerTCPasync.cpp), class renamed to
// GuardedModbusServerTCPasync. The eModbus pin (platformio.ini) still provides ModbusServer,
// ModbusMessage and the logging macros; only the TCP receive/connection handling is ours.
// Upstream master carries the same receive path, so drop this copy once eModbus ships the
// fixes below.
//
// Changes from upstream, all for #277 (task watchdog in async_tcp inside onData):
// - onData() rejects an MBAP length field below 2 (ModbusMbap::checkHeader). Upstream
//   accepted it, so the expected frame was shorter than the 8 bytes already buffered: the
//   frame never completed, no further byte was consumed, and the while (i < len) loop spun
//   in async_tcp until the watchdog reset the device. Any LAN client can send it.
// - onData() validates the header only once 8 bytes are buffered. Upstream validated a
//   partial header, reading the missing length bytes as 0.
// - A bad MBAP header closes the connection. Upstream replied with an error built from the
//   transaction id bytes and kept parsing a stream whose framing was lost.
// - The outbox is capped, so a client that never reads replies cannot exhaust the heap.
// - ~mb_client() frees a partially received request and queued responses, which upstream
//   leaked on every disconnect that left them behind.
// - onClientDisconnect() no longer dereferences or double-deletes a client that stop()
//   already deleted, and onClientConnect() rejects a connection accepted after stop().
#include "modbus_server_tcp_async.h"

#include <algorithm>

#include <modbus_mbap.h>

#define LOCAL_LOG_LEVEL LOG_LEVEL_VERBOSE
#include <Logging.h>

static constexpr size_t MAX_OUTBOX_MESSAGES = 8;

GuardedModbusServerTCPasync::mb_client::mb_client(GuardedModbusServerTCPasync* s, AsyncClient* c) :
  server(s),
  client(c),
  lastActiveTime(millis()),
  message(nullptr),
  error(SUCCESS),
  outbox() {
    client->onData([](void* i, AsyncClient* c, void* data, size_t len) { (static_cast<mb_client*>(i))->onData(static_cast<uint8_t*>(data), len); }, this);
    client->onPoll([](void* i, AsyncClient* c) { (static_cast<mb_client*>(i))->onPoll(); }, this);
    client->onDisconnect([](void* i, AsyncClient* c) { (static_cast<mb_client*>(i))->onDisconnect(); }, this);
    client->setNoDelay(true);
}

GuardedModbusServerTCPasync::mb_client::~mb_client() {
  // clear outbox, if data is left
  while (!outbox.empty()) {
    delete outbox.front();
    outbox.pop();
  }
  delete message;

  delete client;  // will also close connection, if any
}

void GuardedModbusServerTCPasync::mb_client::onData(uint8_t* data, size_t len) {
  lastActiveTime = millis();
  LOG_D("data len %u\n", len);

  Error error = SUCCESS;
  size_t i = 0;
  while (i < len) {
    // 0. start
    if (!message) {
      message = new ModbusMessage(8);
      error = SUCCESS;
    }

    //  1. get minimal 8 bytes to move on
    while (message->size() < ModbusMbap::MIN_FRAME_SIZE && i < len) {
      message->push_back(data[i++]);
    }
    if (message->size() < ModbusMbap::MIN_FRAME_SIZE) {
      LOG_D("header incomplete (len:%d), waiting for next TCP packet\n", message->size());
      return;  // i == len: all data consumed
    }

    // 2. preliminary validation: protocol bytes and message length
    size_t messageLength = 0;
    switch (ModbusMbap::checkHeader(message->data(), &messageLength)) {
    case ModbusMbap::HeaderCheck::BAD_PROTOCOL:
      error = TCP_HEAD_MISMATCH;
      LOG_D("invalid protocol\n");
      break;
    case ModbusMbap::HeaderCheck::BAD_LENGTH:
      error = PACKET_LENGTH_ERROR;
      LOG_D("length error (%u)\n", messageLength);
      break;
    case ModbusMbap::HeaderCheck::OK:
      break;
    }
    if (error != SUCCESS) {
      // Framing is lost, so drop the connection rather than parse the rest as frames.
      // close() deletes this mb_client (and message) through onDisconnect.
      client->close();
      return;
    }

    // 3. receive until request is complete
    while (message->size() < messageLength && i < len) {
      message->push_back(data[i++]);
    }
    if (message->size() == messageLength) {
      LOG_D("request complete (len:%d)\n", message->size());
    } else {
      LOG_D("request incomplete (len:%d), waiting for next TCP packet\n", message->size());
      continue;
    }

    // 4. request complete, process
    ModbusMessage request(messageLength - 6);  // create request without MBAP, with server ID
    request.add(message->data() + 6, message->size() - 6);
    ModbusMessage userData;
    if (server->isServerFor(request.getServerID())) {
      MBSworker callback = server->getWorker(request.getServerID(), request.getFunctionCode());
      if (callback) {
        // request is well formed and is being served by user API
        userData = callback(request);
        // Process Response
        // One of the predefined types?
        if (userData[0] == 0xFF && (userData[1] == 0xF0 || userData[1] == 0xF1)) {
          // Yes. Check it
          switch (userData[1]) {
          case 0xF0: // NIL
            userData.clear();
            LOG_D("NIL response\n");
            break;
          case 0xF1: // ECHO
            userData = request;
            if (request.getFunctionCode() == WRITE_MULT_REGISTERS ||
                request.getFunctionCode() == WRITE_MULT_COILS) {
              userData.resize(6);
            }
            LOG_D("ECHO response\n");
            break;
          default:   // Will not get here!
            break;
          }
        } else {
          // No. User provided data response
          LOG_D("Data response\n");
        }
        error = SUCCESS;
      } else {  // no worker found
        error = ILLEGAL_FUNCTION;
      }
    } else {  // mismatch server ID
      error = INVALID_SERVER;
    }
    if (error != SUCCESS) {
      userData.setError(request.getServerID(), request.getFunctionCode(), error);
    }
    // Keep transaction id and protocol id
    message->resize(4);
    // Add new payload length
    message->add(static_cast<uint16_t>(userData.size()));
    // Append payload
    message->append(userData);
    // Transfer message data to outbox
    addResponseToOutbox(message);
    message = nullptr;
    // A client that pipelines requests but never reads the replies would otherwise grow
    // the outbox (about 20 bytes of heap per input byte) until allocation fails.
    if (outbox.size() > MAX_OUTBOX_MESSAGES) {
      LOG_D("outbox full, closing client\n");
      client->close();  // deletes this mb_client through onDisconnect
      return;
    }
  }  // end while loop iterating incoming data
}

void GuardedModbusServerTCPasync::mb_client::onPoll() {
  // Release obLock before closing: close() deletes this mb_client (and obLock)
  // via the disconnect callback, and destroying a locked mutex leaks it on ESP32.
  {
    LOCK_GUARD(lock1, obLock);
    handleOutbox();
  }
  if (server->idle_timeout > 0 && 
      millis() - lastActiveTime > server->idle_timeout) {
    LOG_D("client idle, closing\n");
    client->close();
  }
}

void GuardedModbusServerTCPasync::mb_client::onDisconnect() {
  LOG_D("client disconnected\n");
  server->onClientDisconnect(this);
}

void GuardedModbusServerTCPasync::mb_client::addResponseToOutbox(ModbusMessage* response) {
  if (response->size() > 0) {
    LOCK_GUARD(lock1, obLock);
    outbox.push(response);
    handleOutbox();
  }
}

void GuardedModbusServerTCPasync::mb_client::handleOutbox() {
  while (!outbox.empty()) {
    ModbusMessage* m = outbox.front();
    if (m->size() <= client->space()) {
      LOG_D("sending (%d)\n", m->size());
      client->add(reinterpret_cast<const char*>(m->data()), m->size(), ASYNC_WRITE_FLAG_COPY);
      client->send();
      delete m;
      outbox.pop();
    } else {
      return;
    }
  }
}

GuardedModbusServerTCPasync::GuardedModbusServerTCPasync() :
  server(nullptr),
  clients(),
  maxNoClients(5),
  idle_timeout(60000) {
    // setup will be done in 'start'
}


GuardedModbusServerTCPasync::~GuardedModbusServerTCPasync() {
  stop();
  delete server;
}


uint16_t GuardedModbusServerTCPasync::activeClients() {
  LOCK_GUARD(lock1, cListLock);
  return clients.size();
}


bool GuardedModbusServerTCPasync::start(uint16_t port, uint8_t maxClients, uint32_t timeout, int coreID) {
  // don't restart if already running
  if (server) {
    LOG_W("Server already running.\n");
    return false;
  }
  
  maxNoClients = maxClients;
  idle_timeout = timeout;
  server = new AsyncServer(port);
  if (server) {
    server->setNoDelay(true);
    server->onClient([](void* i, AsyncClient* c) { (static_cast<GuardedModbusServerTCPasync*>(i))->onClientConnect(c); }, this);
    server->begin();
    LOG_D("Modbus server started\n");
    return true;
  }
  LOG_E("Could not start server\n");
  return false;
}

bool GuardedModbusServerTCPasync::stop() {
  
  if (!server) {
    LOG_W("Server not running.\n");
    return false;
  }
  
  // stop server to prevent new clients connecting
  server->end();

  // now close existing clients
  LOCK_GUARD(lock1, cListLock);
  while (!clients.empty()) {
    // prevent onDisconnect handler to be called, resulting in deadlock
    clients.front()->client->onDisconnect(nullptr, nullptr);
    delete clients.front();
    clients.pop_front();
  }
  delete server;
  server = nullptr;
  LOG_D("Modbus server stopped\n");
  return true;
}

bool GuardedModbusServerTCPasync::isRunning() {
  if (server) return true;
  else return false;
}

void GuardedModbusServerTCPasync::onClientConnect(AsyncClient* client) {
  LOG_D("new client\n");
  LOCK_GUARD(lock1, cListLock);
  // server is null once stop() ran: an accept queued just before it must not revive the server
  if (server && clients.size() < maxNoClients) {
    clients.emplace_back(new mb_client(this, client));
    LOG_D("nr clients: %u\n", clients.size());
  } else {
    LOG_D("max number of clients reached, closing new\n");
    client->close();
    delete client;
  }
}

void GuardedModbusServerTCPasync::onClientDisconnect(mb_client* client) {
  LOCK_GUARD(lock1, cListLock);
  // delete mb_client from list
  // Compared by pointer and deleted only if still listed: stop() on another task may have
  // deleted it while this disconnect waited for cListLock.
  auto it = std::find(clients.begin(), clients.end(), client);
  if (it == clients.end()) return;
  clients.erase(it);
  // delete client itself
  delete client;
  LOG_D("nr clients: %u\n", clients.size());
}
