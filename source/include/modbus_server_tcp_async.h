// =================================================================================================
// eModbus: Copyright 2020 by Michael Harwerth, Bert Melis and the contributors to eModbus
//               MIT license - see license.md for details
// =================================================================================================
// Vendored from eModbus v1.7.6.stable (src/ModbusServerTCPasync.h), renamed to
// GuardedModbusServerTCPasync so it links next to the library's own class. See
// src/modbus_server_tcp_async.cpp for why and what changed.
#pragma once

#include <options.h>

#include <list>
#include <queue>
#if USE_MUTEX
#include <mutex> // NOLINT
#endif
#include <vector>

#include <Arduino.h>
#include <AsyncTCP.h>

#include <ModbusServer.h>

class GuardedModbusServerTCPasync : public ModbusServer {

 private:
  class mb_client {
   friend class GuardedModbusServerTCPasync;

   public:
    mb_client(GuardedModbusServerTCPasync* s, AsyncClient* c);
    ~mb_client();

   private:
    void onData(uint8_t* data, size_t len);
    void onPoll();
    void onDisconnect();
    void addResponseToOutbox(ModbusMessage* response);
    void handleOutbox();
    GuardedModbusServerTCPasync* server;
    AsyncClient* client;
    uint32_t lastActiveTime;
    ModbusMessage* message;
    Modbus::Error error;
    std::queue<ModbusMessage*> outbox;
    #if USE_MUTEX
    std::mutex obLock;  // outbox protection
    #endif
  };


 public:
  GuardedModbusServerTCPasync();

  // Closes the connections
  ~GuardedModbusServerTCPasync();

  // Number of clients currently connected
  uint16_t activeClients();

  // Start the TCP server accepting requests
  bool start(uint16_t port, uint8_t maxClients, uint32_t timeout, int coreID = -1);

  // Drop all connections and stop the server
  bool stop();

  bool isRunning();

 protected:
  void onClientConnect(AsyncClient* client);
  void onClientDisconnect(mb_client* client);

  AsyncServer* server;
  std::list<mb_client*> clients;
  uint8_t maxNoClients;
  uint32_t idle_timeout;
  #if USE_MUTEX
  std::mutex cListLock;  // client list protection
  #endif
};
