## Hands-On Walkthrough of the System and Setup

This section will guide you through the setup of the system and the hands-on walkthrough of the system.

### System Setup

#### Required Hardware
1. Heltec WiFi LoRa 32(V3) development board
2. INA219 current sensor
3. External power supply (5V) for Power Measurement

#### Step 1: Install the required software

1. Clone the repository to your local machine
2. Install the ESP-IDF toolchain, using the VSCode extension or the command line
3. On the /edge-server folder, create a virtual environment and install the required Python packages to run the edge server, using the pip install -r requirements.txt command
4. Install Mosquitto MQTT Broker on your machine
5. Inside the /edge-server/mqtt_broker/certs folder, change the san.cnf file and generate_certs.sh file replacing the 192.168.86.94 IP address with your machine's IP address (ifconfig on Linux or ipconfig on Windows)
6. Run the generate_certs.sh script to generate the certificates
7. Change the directory of the certificates in the mosquitto.conf file to the /edge-server/mqtt_broker folder and on the edge_server.py file to the /edge-server folder
8. In the mqtt.c file, change the IP address of the MQTT Broker to your machine's IP address and update the certificates to use the newly generated certificates in the /edge-server/mqtt_broker/certs folder, by copying the contents of ca_cert.pem, client_cert.pem and client_key.pem to the corresponding variables in the mqtt.c file
9. Install the ESP-IDF-LIB library by running the procedure described in the [ESP-IDF-LIB repository](
    https://github.com/UncleRus/esp-idf-lib
)
10. Install the ESP-DSP library by running the procedure described in the [ESP-DSP repository](
    https://github.com/espressif/esp-dsp
)

#### Step 2: Configure the ESP32 using the ESP-IDF toolchain

1. Set the Espressif Device Target to esp32s3
2. Run idf.py set-target esp32s3
3. It is necessary to change the CONFIG_FREERTOS_HZ from 100Hz to 1000Hz by changing the variable in: idf.py menuconfig -> component config -> FreeRTOS -> Tick rate (hz)

#### Step 3: Connect the INA219 sensor between the Heltec WiFi LoRa 32(V3) and the Power Supply

The INA219 sensor should be connected between the Heltec WiFi LoRa 32(V3) and the external power supply, in series with the power supply. The Vin+ pin receives the positive terminal of the power supply, while the Vin- pin gets connected to the 5V pin of the Heltec WiFi LoRa 32(V3). The negative terminal of the power supply should be connected to the GND pin of the Heltec WiFi LoRa 32(V3). As for the other INA219 pins, the SDA pin should be connected to the GPIO1 pin while the SCL pin should be connected to the GPIO2 pin, and the Vcc pin should be connected to the 3.3V pin of the ESP32 while the GND pin should be connected to the GND pin of the ESP32.

Note: the flag bool power_measurement_active = true; in main.c needs to be set to true to enable the power measurement feature. It will work if the ESP32 is powered by the USB port, but the measurements won't be correct.


#### Step 4. Run the Edge Server and the MQTT Broker

1. Run the Mosquitto MQTT Broker on your machine by going to the /edge-server/mqtt_broker folder and running the "mosquitto -v -c mosquitto.conf" command
2. Run the Edge Server on your machine by going to the /edge-server folder and running the "python edge_server.py" command (while on the virtual environment where the pip packages were installed)

#### Step 4: Build and Flash the ESP32

1. Connect the Heltec WiFi LoRa 32(V3) to your computer
2. Run idf.py build to build the project
3. Run idf.py flash monitor

#### Step 5. Observe the results being displayed on the terminal

Run once with the flag bool power_measurement_active = false; and observe the results on the idf.py flash monitor terminal. Then, run again with the flag bool power_measurement_active = true; and observe the results for the power measurement feature on the edge server terminal.