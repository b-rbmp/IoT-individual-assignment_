import paho.mqtt.client as mqtt
from datetime import datetime
import json
from pydantic import BaseModel

# MQTT Callbacks
def on_connect(client, userdata, flags, rc):
    """
    Callback function that is called when the client successfully connects to the broker.

    Parameters:
        client (mqtt.Client): The client instance for this callback.
        userdata: The private user data as set in the `mqtt.Client` constructor.
        flags: Response flags sent by the broker.
        rc (int): The connection result code.

    Subscribes to the topic where the average data is published (/average).
    Subscribes to the topic where the energy data is published (/energy).

    Returns:
        None

    """
    if rc == 0:
        print("Connected successfully to broker")

        # Subscribe to the topic where the average data is published (/average)
        client.subscribe("/average")

        # Subscribe to the topic where the energy data is published (/energy)
        client.subscribe("/energy")
    else:
        print("Connect failed with code", rc)


def on_message(client, userdata, msg):
    """
    Callback function that is called when a message is received.

    Args:
        client: The MQTT client instance that received the message.
        userdata: Any user-defined data that was passed to the MQTT client.
        msg: The received message object.

    On this function, the received AverageData over the topic /average is validated and printed.

    Returns:
        None

    Raises:
        None
    """
    print(f"Message received {msg.payload}")
    try:
        data = json.loads(msg.payload)
        
        # If topic is /energy, print the energy data
        if msg.topic == "/energy":
            # Validate incoming data
            data = EnergyData(
                node_id=data['node_id'],
                energy_optimal=data['energy_optimal'],
                energy_original=data['energy_original'],
                details=data['details'],
            )
            print(f"Energy data received: {data}")
            return
        elif msg.topic == "/average":
            # Validate incoming data
            data = AverageData(
                node_id=data['node_id'],
                aggregation_result=data['aggregation_result'],
            )
            print(f"Valid data received: {data}")
    except Exception as e:
        print(f"Invalid data: {e}")

# Pydantic model for the average data, containing the node_id and the aggregation_result
class AverageData(BaseModel):
    node_id: str
    aggregation_result: str

class EnergyData(BaseModel):
    node_id: str
    energy_optimal: str
    energy_original: str
    details: str

# Initialize MQTT Client
client = mqtt.Client()

# Set TLS/SSL parameters
client.tls_set(
    ca_certs="/home/bernardoribeiro/Documents/GitHub/IoT-individual-assignment_/edge-server/mqtt_broker/certs/ca_cert.pem",
    certfile="/home/bernardoribeiro/Documents/GitHub/IoT-individual-assignment_/edge-server/mqtt_broker/certs/client_cert.pem",
    keyfile="/home/bernardoribeiro/Documents/GitHub/IoT-individual-assignment_/edge-server/mqtt_broker/certs/client_key.pem",
    tls_version=mqtt.ssl.PROTOCOL_TLS,
)

# Set the callbacks
client.on_connect = on_connect
client.on_message = on_message

# Connect to the broker using secure port
client.connect("localhost", 8883, 60)
client.loop_start()

# Keep the script running
while True:
    pass
