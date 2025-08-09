import json
import boto3
import os
import logging
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
# No need for 're', 'sys', 'base64' as they were unused in the first function

# --- Configure Logging ---
logger = logging.getLogger()
# Set log level from env var, default INFO. Convert to upper case for consistency.
logger.setLevel(os.environ.get("LOG_LEVEL", "INFO").upper())

# --- Global/Client Initialization (outside handler for warm starts) ---
# InfluxDB Configuration
# Ensure these environment variables are set in your Lambda configuration
MEASUREMENT = "meter"
INFLUXDB_TOKEN = os.environ.get("INFLUXDB_TOKEN")
INFLUXDB_ORG = os.environ.get("INFLUXDB_ORG")
INFLUXDB_BUCKET = os.environ.get("INFLUXDB_BUCKET")
INFLUXDB_URL = os.environ.get("INFLUXDB_URL")

# Kinesis Firehose Configuration
DELIVERY_STREAM_NAME = os.environ.get('DELIVERY_STREAM_NAME')

# Initialize clients (only once per Lambda execution environment)
influxdb_client = None
firehose_client = None

def init_clients():
    """Initializes InfluxDB and Firehose clients. Called once per execution environment."""
    global influxdb_client, firehose_client
    if influxdb_client is None:
        if not all([INFLUXDB_URL, INFLUXDB_TOKEN, INFLUXDB_ORG, INFLUXDB_BUCKET]):
            logger.error("Missing one or more InfluxDB environment variables. InfluxDB client will not be initialized.")
        else:
            try:
                influxdb_client = InfluxDBClient(url=INFLUXDB_URL, token=INFLUXDB_TOKEN, org=INFLUXDB_ORG)
                logger.info("InfluxDB client initialized.")
            except Exception as e:
                logger.error(f"Failed to initialize InfluxDB client: {e}", exc_info=True)
                influxdb_client = None # Ensure it's None if initialization fails
    
    if firehose_client is None:
        if not DELIVERY_STREAM_NAME:
            logger.error("Missing DELIVERY_STREAM_NAME environment variable. Firehose client will not be initialized.")
        else:
            try:
                firehose_client = boto3.client('firehose')
                logger.info("Firehose client initialized.")
            except Exception as e:
                logger.error(f"Failed to initialize Firehose client: {e}", exc_info=True)
                firehose_client = None # Ensure it's None if initialization fails


def lambda_handler(event, context):
    request_id = context.aws_request_id
    logger.info(f"[{request_id}] Lambda function started")
    logger.debug(f"[{request_id}] Received event: {json.dumps(event)}")

    # Ensure clients are initialized (will only run if they are None)
    init_clients()

    if influxdb_client is None and firehose_client is None:
        logger.error(f"[{request_id}] Neither InfluxDB nor Firehose clients could be initialized. Cannot process event.")
        return {
            'statusCode': 500,
            'body': json.dumps('Failed to initialize clients, event not processed.')
        }

    # --- Common Event Parsing ---
    iot_message = event # Assuming direct payload from IoT Core
    meter_payloads = iot_message.get('payload', [])
    original_topic = iot_message.get('topic', 'unknown_topic')

    if not meter_payloads:
        logger.info(f"[{request_id}] No meter_payloads found in the event. Exiting.")
        return {'statusCode': 200, 'body': json.dumps('No payloads to process')}

    logger.info(f"[{request_id}] Processing {len(meter_payloads)} data points from topic: {original_topic}")

    device_id_parts = original_topic.split('/')
    device_id = device_id_parts[2] if len(device_id_parts) > 2 else "unknown_device"
    if device_id == "unknown_device":
        logger.warning(f"[{request_id}] Could not determine device_id from topic: {original_topic}")

    # --- Data structures for each destination ---
    influx_points = []
    firehose_transformed_json_strings = []
    firehose_record_type_counts = {} # To count different types of records processed

    # --- Process each message in the payload ---
    for message in meter_payloads:
        logger.debug(f"[{request_id}] Processing message: {message}")

        # --- Detect new format arrays ---
        if isinstance(message, list):
            try:
                # Expected: [unixTime, channel, activePower, powerFactor]
                unixTime = int(message[0])
                channel = int(message[1])
                activePower = float(message[2])
                powerFactor = float(message[3])

                # --- InfluxDB Point ---
                point = Point(MEASUREMENT) \
                    .tag("device_id", device_id) \
                    .tag("channel", channel) \
                    .time(unixTime, write_precision="ms" if unixTime > 1e11 else "s") \
                    .field("active_power", activePower) \
                    .field("power_factor", powerFactor)
                influx_points.append(point)

                # --- Firehose record ---
                transformed_item = {
                    'unixtime': unixTime,
                    'device_id': device_id,
                    'channel': channel,
                    'active_power': activePower,
                    'power_factor': powerFactor,
                    'active_energy_imported': None,
                    'active_energy_exported': None,
                    'reactive_energy_imported': None,
                    'reactive_energy_exported': None,
                    'apparent_energy': None,
                    'voltage': None,
                    'record_type': 'power_reading'
                }
                firehose_record_type_counts['power_reading'] = firehose_record_type_counts.get('power_reading', 0) + 1
                firehose_transformed_json_strings.append(json.dumps(transformed_item))
            except Exception as e:
                logger.error(f"[{request_id}] Error processing array-format message {message}: {e}", exc_info=True)
            continue  # Skip old-format logic for arrays

        # --- Legacy / object-based format ---
        try:
            unixTime = int(message['unixTime'])
        except (KeyError, ValueError):
            logger.warning(f"[{request_id}] Skipping message without valid unixTime: {message}")
            continue

        try:
            point = Point(MEASUREMENT).tag("device_id", device_id).time(
                unixTime, write_precision="ms" if unixTime > 1e11 else "s"
            )

            if 'voltage' in message:
                voltage = float(message['voltage'])
                if voltage != 0:
                    point.field("voltage", voltage)

            if 'current' in message:
                point.field("current", float(message['current']))
            if 'activePower' in message:
                point.field("active_power", float(message['activePower']))
            if 'reactivePower' in message:
                point.field("reactive_power", float(message['reactivePower']))
            if 'apparentPower' in message:
                point.field("apparent_power", float(message['apparentPower']))
            if 'powerFactor' in message:
                point.field("power_factor", float(message['powerFactor']))

            if 'activeEnergyImported' in message:
                point.field("active_energy_imported", float(message['activeEnergyImported']))
            if 'activeEnergyExported' in message:
                point.field("active_energy_exported", float(message['activeEnergyExported']))
            if 'reactiveEnergyImported' in message:
                point.field("reactive_energy_imported", float(message['reactiveEnergyImported']))
            if 'reactiveEnergyExported' in message:
                point.field("reactive_energy_exported", float(message['reactiveEnergyExported']))
            if 'apparentEnergy' in message:
                point.field("apparent_energy", float(message['apparentEnergy']))

            channel = message.get('channel')
            if channel is not None:
                point.tag("channel", channel)

            influx_points.append(point)
        except Exception as e:
            logger.error(f"[{request_id}] Error creating InfluxDB point for message {message}: {e}", exc_info=True)

        # --- Firehose transformation ---
        try:
            transformed_item = {
                'unixtime': message.get('unixTime'),
                'device_id': device_id,
                'channel': message.get('channel'),
                'active_power': message.get('activePower'),
                'power_factor': message.get('powerFactor'),
                'active_energy_imported': message.get('activeEnergyImported'),
                'active_energy_exported': message.get('activeEnergyExported'),
                'reactive_energy_imported': message.get('reactiveEnergyImported'),
                'reactive_energy_exported': message.get('reactiveEnergyExported'),
                'apparent_energy': message.get('apparentEnergy'),
                'voltage': message.get('voltage'),
            }

            if "activePower" in message and "powerFactor" in message:
                transformed_item['record_type'] = "power_reading"
            elif "activeEnergyImported" in message:
                transformed_item['record_type'] = "energy_summary"
            elif "voltage" in message:
                transformed_item['record_type'] = "voltage_reading"
            else:
                transformed_item['record_type'] = "unknown"

            firehose_record_type_counts[transformed_item['record_type']] = firehose_record_type_counts.get(transformed_item['record_type'], 0) + 1
            firehose_transformed_json_strings.append(json.dumps(transformed_item))
        except Exception as e:
            logger.error(f"[{request_id}] Error transforming message for Firehose {message}: {e}", exc_info=True)

    # --- Attempt to write to InfluxDB ---
    influxdb_success = False
    if influxdb_client and influx_points:
        write_api = influxdb_client.write_api(write_options=SYNCHRONOUS)
        try:
            logger.debug(f"[{request_id}] Writing {len(influx_points)} points to InfluxDB")
            write_api.write(bucket=INFLUXDB_BUCKET, record=influx_points)
            logger.info(f"[{request_id}] Successfully wrote {len(influx_points)} points to InfluxDB.")
            influxdb_success = True
        except Exception as e:
            logger.error(f"[{request_id}] Error writing points to InfluxDB: {e}", exc_info=True)
    elif not influx_points:
        logger.info(f"[{request_id}] No InfluxDB points to write after processing. (Possible voltage 0 skips)")
        influxdb_success = True # Consider success if no points to write for this path
    else:
        logger.warning(f"[{request_id}] InfluxDB client not available or not initialized.")
        influxdb_success = False

    # --- Attempt to send to Firehose ---
    firehose_success = False
    if firehose_client and firehose_transformed_json_strings:
        logger.info(f"[{request_id}] Transformed record counts by type for Firehose: {json.dumps(firehose_record_type_counts)}")
        
        # Concatenate JSON strings to make efficient use of Firehose: {"a":1}{"a":2}...
        concatenated_json_data = "".join(firehose_transformed_json_strings)
        concatenated_data_bytes = concatenated_json_data.encode('utf-8')
        logger.info(f"[{request_id}] Total size of concatenated JSON data to send to Firehose: {len(concatenated_data_bytes)} bytes for {len(firehose_transformed_json_strings)} logical records.")

        # Send this single concatenated string as ONE record to Firehose
        firehose_single_batch_record = [{'Data': concatenated_data_bytes}]
        firehose_success = send_to_firehose(firehose_single_batch_record, request_id)
    elif not firehose_transformed_json_strings:
        logger.info(f"[{request_id}] No items were transformed for Firehose. Exiting Firehose path.")
        firehose_success = True # Consider success if no records to send for this path
    else:
        logger.warning(f"[{request_id}] Firehose client not available or not initialized.")
        firehose_success = False


    # --- Determine overall Lambda outcome ---
    if influxdb_success and firehose_success:
        logger.info(f"[{request_id}] Lambda function finished successfully for both destinations.")
        return {
            'statusCode': 200,
            'body': json.dumps('Successfully processed event for both InfluxDB and Firehose.')
        }
    else:
        # If either operation failed, indicate failure to IoT Core
        logger.error(f"[{request_id}] Lambda function finished with partial or complete failure. InfluxDB success: {influxdb_success}, Firehose success: {firehose_success}")
        return {
            'statusCode': 500,
            'body': json.dumps('Failed to process event for one or more destinations.')
        }

# --- Helper function for Firehose (retained from original) ---
def send_to_firehose(records_batch, request_id: str) -> bool:
    """
    Sends a batch of records (expected to be a list containing a single concatenated record)
    to Kinesis Data Firehose.
    Returns True on success, False on failure.
    """
    if not records_batch:
        logger.warning(f"[{request_id}] No records in batch to send to Firehose.")
        return True # No action needed, considered success for this function's purpose

    try:
        response = firehose_client.put_record_batch(
            DeliveryStreamName=DELIVERY_STREAM_NAME,
            Records=records_batch
        )
        logger.debug(f"[{request_id}] Firehose PutRecordBatch raw response: {response}")

        failed_put_count = response.get('FailedPutCount', 0)
        if failed_put_count > 0:
            logger.error(f"[{request_id}] Failed to put {failed_put_count} records (out of {len(records_batch)}) to Firehose.")
            for i, req_resp in enumerate(response.get('RequestResponses', [])):
                if req_resp.get('ErrorCode'):
                    logger.error(
                        f"[{request_id}] Record index {i} failed with ErrorCode: {req_resp['ErrorCode']}, "
                        f"ErrorMessage: {req_resp.get('ErrorMessage', 'N/A')}"
                    )
            return False
        else:
            logger.info(f"[{request_id}] Successfully sent {len(records_batch)} record(s) (one batch) to Firehose.")
            return True

    except Exception as e:
        logger.error(f"[{request_id}] Exception sending batch to Firehose: {e}", exc_info=True)
        return False