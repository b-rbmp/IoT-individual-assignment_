#include "mqtt.h"
#include "mqtt_client.h"
#include "esp_log.h"

// MQTT Configuration
esp_mqtt_client_handle_t client = NULL;
static const char *MQTTTAG = "MQTT";                     // Tag used for Logging
static bool mqtt_connected = false;                      // Boolean flag to indicate whether mqtt is connected or not
const char *mqtt_address = "mqtts://192.168.86.94:8883"; // MQTT address
#define NODE_ID "node000000"                             // Node ID for the device

// Certificates

// Root CA Certificate
const char *ca_cert =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIC/zCCAeegAwIBAgIUXLSbvmL05l9iaasB8Npef3WrrZwwDQYJKoZIhvcNAQEL\n"
    "BQAwDzENMAsGA1UEAwwETXlDQTAeFw0yNDA1MjExNjA4MjRaFw0yNzAzMTExNjA4\n"
    "MjRaMA8xDTALBgNVBAMMBE15Q0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK\n"
    "AoIBAQDbzLoPG9BoX3YvJBkpCgYLnpm1/tD/qWzfkU64SL2Frp7YC1RN8lncg+HN\n"
    "TvhupqA94RpZ/a0qYnasDHDJdEozwGVKsM96DVT1OJFTSeheGcX7RXdR31KenVxd\n"
    "NddONgdjoTa+5xZiwN1jCpc0FeW1NVEPb5ceTx0hG2ii/6+ef0PRt5Bs2/W3iqfx\n"
    "44yGsJWZZUgs/fQ+hol5I6UuBESKFxguqJdGVtIS6hy0bw2b7hggeeAxqONHkQOp\n"
    "CXbwckqIFAv63tOw+olr7vyAKW1/cOMMgOqCQwjPgf2R4uyhi0tzhGpA6rQ8MJnF\n"
    "+4DzMBhQnlG/2saZ+D22hyG1/m5vAgMBAAGjUzBRMB0GA1UdDgQWBBQmem3H1KmB\n"
    "aPGTf9Hd3bZGBNb4nDAfBgNVHSMEGDAWgBQmem3H1KmBaPGTf9Hd3bZGBNb4nDAP\n"
    "BgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBAQC+bfZa6ly+ulVleQ7/\n"
    "C8RXGKXOsfoSiBuDf0xWq/zDK12F2BH0tBuwGpoJDxUgUwZFKRgMh7S2Rn0Az56J\n"
    "2GJEjQG06VQIuy0s/mduU1gxyJvA45X4BfQ1odKFJ2yH3ZTeDVA1nRQzW7C35Sax\n"
    "NMPxeDSRyFFWAWbCV8fyRJ/Sa66SpzkjXMhwA/551VOqATW0uTqrSA1Zbr2yBvLx\n"
    "AmRQjOaG51WftnOX0Iwhb9U9VdFbwjJPwN63lsYt5fhuAc9uRDugNn0EVnMKgPwb\n"
    "yrHuiTaK21B/XanvfGz3b1rZ8MWsg8dhdEhIoKjxa5507c+iw+JNRQs6i0y+SnU2\n"
    "VY64\n"
    "-----END CERTIFICATE-----\n";


// Client Certificate
const char *client_cert =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIC8DCCAdigAwIBAgIUQlqDoHiedpFfDOTXY8YeHbqrsEYwDQYJKoZIhvcNAQEL\n"
    "BQAwDzENMAsGA1UEAwwETXlDQTAeFw0yNDA1MjExNjA4MjRaFw0yNTEwMDMxNjA4\n"
    "MjRaMBExDzANBgNVBAMMBmNsaWVudDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCC\n"
    "AQoCggEBAOgEN9u8oTduKm9sin+8pmDIx3gbpUw1iAIomFXkXuBjMigG7sS6eaZY\n"
    "eLQdi3C2pItsmYzKWPSiTF0kiOXYS1J8fUkEdswWHCjI+rxlqPcHApvvG5tNEjVz\n"
    "zPDtyECSQ21JAkVmi2t/yUa9+s4EmagIm/bnEfFX6yiRiFyAjHggCxIIg5dfls+m\n"
    "/ZLJ/UMnx99ehYHOHJRwUGgyO5Wz1w7AOaVn05l8KuaRNuzgYJeDS8boqvnlqOF5\n"
    "Fzqgd+jnvl4VSuXBrWbwxzMWydA/dhayFmnawIGI/WhoeRQXq3x9ee4WMDOhC3zT\n"
    "JDdFeER4GMjqX30v29kaExwIDcQDY+ECAwEAAaNCMEAwHQYDVR0OBBYEFAPiXZ9i\n"
    "aL3NR0CdCRZLxaIZWAwLMB8GA1UdIwQYMBaAFCZ6bcfUqYFo8ZN/0d3dtkYE1vic\n"
    "MA0GCSqGSIb3DQEBCwUAA4IBAQALxcyh/mvwl/nZUtcUywFiGOwfW99bnUJBoe5P\n"
    "fs0K2e/nA0aGkm91/mkO15Q2SuV11D5rmChtnuAiaSknYJDBGHHaIn8svA83jjkH\n"
    "tfo8ys3NePScJ2KAqExAKgAUQp6qx2QPneppZ57Pgeuq60SwJwQs2fLHbt5+1VVP\n"
    "/mRCjbVYzYw+SBaF54EWfzkcB5c7KHtxqwEZ6xfgsqznjvJ8Ag0gEp//FnaAnhC6\n"
    "1KB5zETmtmjvNMWK0BMWkdaqFOoFksuo5jEQiIzA9Bt7iZK1pjhRLm8HFHfHJqB0\n"
    "K7UsitSjeTwXXlUCqYBAeBN9QQ8ix/htlyju0H29XGXFpnje\n"
    "-----END CERTIFICATE-----\n";


// Client Key
const char *client_key =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDoBDfbvKE3bipv\n"
    "bIp/vKZgyMd4G6VMNYgCKJhV5F7gYzIoBu7EunmmWHi0HYtwtqSLbJmMylj0okxd\n"
    "JIjl2EtSfH1JBHbMFhwoyPq8Zaj3BwKb7xubTRI1c8zw7chAkkNtSQJFZotrf8lG\n"
    "vfrOBJmoCJv25xHxV+sokYhcgIx4IAsSCIOXX5bPpv2Syf1DJ8ffXoWBzhyUcFBo\n"
    "MjuVs9cOwDmlZ9OZfCrmkTbs4GCXg0vG6Kr55ajheRc6oHfo575eFUrlwa1m8Mcz\n"
    "FsnQP3YWshZp2sCBiP1oaHkUF6t8fXnuFjAzoQt80yQ3RXhEeBjI6l99L9vZGhMc\n"
    "CA3EA2PhAgMBAAECggEAXOVTeCeOZSM22mYbDgn6k8XQF8b56AmG61ZVqA5H7o/J\n"
    "BT3BXANNscy6h2NM8VQvjaNt13EtLMXOzXxTU7PGdMBjaVjgN9aib7IIsmYBwOaN\n"
    "pOGXrBavs7cp101dYH0vghI5VpA4QlJnxGtMgDBXVXAdAj6CcJ9DCHs8nczacT2f\n"
    "2xU2wEWg3Ij2DkVy/Y6njVKMpQiJtY8+Af6lKgPUasZMxAWwYT/xeqAKAtvXmrV8\n"
    "ipzB7OObQK4ZSUiFktV/HKTT5PdROR6cRLaW3Fo12pT59V5xwnzfWkEq6m4+i2kv\n"
    "NHrLtCDJlejA3MqH9lj5y4zc88d2alUtRf5kP8L6AQKBgQD+QE7ZDHmaRQSKUYIC\n"
    "dVMIq1+rownXHmonZ0ua7bhhG09IZ/EVWACNELIckiK+Q3UzonJEYRAK9O3QJI8R\n"
    "GgXidgSgeps1F3daR7LYdSg7aRzhqiDn4LPBu5TzUWl/djdBRfJaknbMwrERpA/1\n"
    "svJnVFEsBGregzvXFfwBsDEYnwKBgQDpnMI70N2L0WgzVy2+NXa5717A+gh/jEI5\n"
    "EcrFuRC0j0cw08MILafD+epL9xfpmdvqj5xdwu1DqGTvwXIJgdFzJOebVm+ejNKB\n"
    "LgQkXvuUE0C2hWtqag5Yy7p/F1oamn3YNhdYSzCC4Fqrfx4MjNSFZEyhW7E4yoQX\n"
    "iz2WFe+zfwKBgCnIQ6zjofA6O17HukfRJA4eq6A3MNzLQMKs9P3G5y/+Hu9VNYc+\n"
    "aQjFn5+WXGNMDqynm4OlN8+1JIe6GlDDKAhpQKVmwK7e5lxNwBRWXIA26+oh3Tp9\n"
    "8Mx7mSTLUj0CHl55sjQ0MZXAwPyXK5BDEhbtAlHrf+yFQIY+NKXKYKcdAoGAKygN\n"
    "bdVxCCJgWJOzXJ8t1r2UyJuoND/p//b8ebhtkJ2jbi7AqeMdSgQCN5RnM3179gIf\n"
    "xeZ7xHL4ap5W/dCgq9/WdYjrR+QGA11H4Jw3Z6yC2PUe2eLL0bWZN19OAvolY2ri\n"
    "tyn0xUjDF0l9eJ4PqLDVwz1YKQColFzhvLdCt+sCgYEAr4z/18V2yVKZ3hJR2Aaw\n"
    "Aep9t4dfwXMku1PoturOVivKIukMDAIZw3zI1V3xtvO+IoPBjpO6UcdEU3gUcRt0\n"
    "aNWraDZlb4vhlIYYux+qRWiSnY1P0I8A3ZjxJt1WZeNWxBZ8I8T4rxceSO0D5/3H\n"
    "867nJSRmc2C85AGTi11d7nU=\n"
    "-----END PRIVATE KEY-----\n";


/**
 * @brief Logs an error message if the error code is non-zero.
 *
 * This function logs an error message using the ESP_LOGE macro if the provided error code is non-zero.
 *
 * @param message The error message to be logged.
 * @param error_code The error code to be checked.
 */
void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(MQTTTAG, "Last error %s: 0x%x", message, error_code);
    }
}

/**
 * @brief Publishes a message to a specified MQTT topic.
 *
 * This function publishes a message to the specified MQTT topic if the MQTT client is connected.
 * If the client is not connected, an error message is logged.
 *
 * @param topic The MQTT topic to publish the message to.
 * @param data The message data to be published.
 */
void mqtt_publish(char *topic, char *data, int qos, int retain)
{
    if (mqtt_connected)
    {
        // Only log the message if the QoS is 0, otherwise only show the latency
        if (qos == 0)
        {
            ESP_LOGI(MQTTTAG, "Publishing message to topic %s, with QoS %d: %s\n", topic, qos, data);
        }
        esp_mqtt_client_publish(client, topic, data, 0, qos, retain);
    }
    else
    {
        ESP_LOGE(MQTTTAG, "MQTT not connected. Cannot publish message.");
    }
}
