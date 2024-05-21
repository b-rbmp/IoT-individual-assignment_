#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include "esp_timer.h"
#include "esp_dsp.h"
#include "esp_log.h"
#include "config.c"
#include "mqtt.c"
#include "nvs_flash.h"
#include <ina219.h>

// Boolean that triggers the power measurement. Should be set to false when the ESP32 is connected via USB.
bool power_measurement_active = true;

// Define the number PI for sine calculation
#define PI 3.14159265

// Define the tag for ESP_LOGx output
static const char *TAG = "main";

// Define the number of samples of the signal stored in memory
#define N_SAMPLES 4096
int N = N_SAMPLES;

// We assume that the original sampling frequency of the signal is 100Hz, prior to being stored in memory, although it is 
// simulated in the firmware. This is used to calculate the time between each sample, for FFT and other calculations.
#define SIGNAL_ORIGINAL_SAMPLING_FREQUENCY 100
#define TIME_BETWEEN_SAMPLES (1.0 / SIGNAL_ORIGINAL_SAMPLING_FREQUENCY)

// Window coefficients
__attribute__((aligned(16))) float wind[N_SAMPLES];
// Signal array
__attribute__((aligned(16))) float signal_[N_SAMPLES];
// FFT working complex array
__attribute__((aligned(16))) float y_cf[N_SAMPLES * 2];
// Power spectrum array
__attribute__((aligned(16))) float power_spectrum[N_SAMPLES];

// Latency measurement
int64_t publish_start_time = 0;
bool measuring_latency = false;


// INA219 variables
#define I2C_PORT 0
#define CONFIG_EXAMPLE_SHUNT_RESISTOR_MILLI_OHM 100
#define CONFIG_EXAMPLE_I2C_ADDR 0x40
#define CONFIG_EXAMPLE_I2C_MASTER_SCL 2
#define CONFIG_EXAMPLE_I2C_MASTER_SDA 1
#define I2C_ADDR CONFIG_EXAMPLE_I2C_ADDR
ina219_t dev;
float power;

// Structure for Power Measurement Data
typedef struct {
    float *power_values; // Array to store power values
    int max_samples; // Maximum number of samples to measure
    int current_sample_count; // Current number of samples measured
    bool measuring; // Flag to indicate if power measurement is active
    int64_t start_time; // Start time of the measurement
    int64_t end_time; // End time of the measurement
} power_measurement_t;

// Structure for Power Measurement Result
typedef struct {
    float average_power; // Average power value
    float total_energy_wh; // Total energy in Wh
} power_measurement_result_t;

// Global power measurement control structure
power_measurement_t pm;

// Define a function pointer type for the signal generation function
typedef float (*signal_function_t)(float t);

// Input Signal 1: 2*sin(2*pi*3*t)+4*sin(2*pi*5*t)
float input_signal_1(float t) {
    return 2 * sin(2 * PI * 3 * t) + 4 * sin(2 * PI * 5 * t);
}

// Input Signal 2: 1*sin(2*pi*2*t) + 2*sin(2*pi*20*t) + 3*sin(2*pi*100*t)
float input_signal_2(float t) {
    return sin(2 * PI * 2 * t) + 2 * sin(2 * PI * 20 * t) + 3 * sin(2 * PI * 100 * t);
}

// Input Signal 3: 3sin(2*pi*150*t)
float input_signal_3(float t) {
    return 3 * sin(2 * PI * 150 * t);
}

/**
 * @brief Samples a signal with fixed memory allocation.
 *
 * This function generates a signal by sampling a signal function.
 * The signal is sampled at a specified sampling rate, and the samples are stored in the output array.
 * A delay is introduced between each sample to control the sampling rate.
 *
 * @param output Pointer to the output array where the samples will be stored.
 * @param len The length of the output array.
 * @param sampling_rate The sampling rate in samples per second.
 * @param signal_func A pointer to a function that generates the signal value for a given time `t`.
 * @return Pointer to the output array containing the sampled signal.
 */
float* sample_signal_fixed_with_delay(float *output, int len, float sampling_rate, signal_function_t signal_func) {
    ESP_LOGI(TAG, "Sampling signal with fixed memory allocation...");

    // Calculate the time between samples in milliseconds
    float time_between_samples_ms = 1000.0 / sampling_rate;

    // Generate the signal samples
    for (int i = 0; i < len; i++) {
        float t = i / sampling_rate;  // Calculate the time for each sample
        output[i] = signal_func(t);

        // Percentage of samples completed
        float percent_complete = (float)(i + 1) / len * 100;

        // Only log every 50 samples
        if (i % 50 == 0) {
            ESP_LOGI(TAG, "Taking Sample %i: %f | %.2f%% complete", i + 1, output[i], percent_complete);
        }
        
        // Delay until the next sample time
        vTaskDelay(pdMS_TO_TICKS(time_between_samples_ms));
    }

    // Return the pointer to the signal array
    return output;
}

/**
 * @brief Samples a signal with dynamic memory allocation.
 *
 * This function generates a signal by sampling a signal function
 * The signal is sampled at a specified sampling frequency for a given time window.
 * The number of samples is calculated based on the sampling frequency and time window.
 * The function dynamically allocates memory to store the samples.
 * Each sample is calculated using the sine wave equations and stored in the allocated memory.
 * The progress of sampling is logged, and a delay is introduced between samples to simulate real-time sampling.
 *
 * @param sampling_frequency The sampling frequency in Hz.
 * @param time_window The time window for sampling in seconds.
 * @param out_num_samples Pointer to store the number of samples.
 * @param signal_func A pointer to a function that generates the signal value for a given time `t`.
 * @return A pointer to the dynamically allocated array containing the signal samples.
 *         Returns NULL if memory allocation fails.
 */
float* sample_signal_dynamic_with_delay(signal_function_t signal_func, float sampling_frequency, float time_window, int* out_num_samples) {
    ESP_LOGI(TAG, "Sampling signal with dynamic memory allocation...");

    // Calculate the number of samples
    int num_samples = (int)(sampling_frequency * time_window);
    *out_num_samples = num_samples;

    // Dynamically allocate an array to store the samples
    float* signal_ = (float*)malloc(num_samples * sizeof(float));
    if (signal_ == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for signal");
        return NULL;
    }

    // Calculate the time between samples in milliseconds
    float time_between_samples_ms = 1000.0 / sampling_frequency;

    // Generate the signal samples
    for (int i = 0; i < num_samples; i++) {
        float t = i / sampling_frequency;  // Calculate the time for each sample
        signal_[i] = signal_func(t);

        // Percentage of samples completed
        float percent_complete = (float)(i + 1) / num_samples * 100;

        // Log the progress every 50 samples
        if (i % 50 == 0) {
            ESP_LOGI(TAG, "Taking Sample %i: %f | %.2f%% complete", i + 1, signal_[i], percent_complete);
        }

        // Delay until the next sample time
        vTaskDelay(pdMS_TO_TICKS(time_between_samples_ms));
    }

    // Return the pointer to the signal array
    return signal_;
}

/**
 * @brief Stores the generated signal in memory after applying a Hann window.
 * 
 * This function generates an input signal based on a signal function and stores it in memory (signal) with a certain sampling frequency.
 * It then applies a Hann window to the signal and stores the result in the complex array y_cf.
 * The imaginary part of each element in y_cf is set to zero.
 * 
 */
void store_signal(signal_function_t signal_func, int sampling_frequency) {
    // Generate input signal and store it in memory (signal) with a certain sampling frequency
    sample_signal_fixed_with_delay(signal_, N, sampling_frequency, signal_func);

    // Apply hann window to the signal
    dsps_wind_hann_f32(wind, N);
    for (int i = 0; i < N; i++) {
        y_cf[i * 2 + 0] = signal_[i] * wind[i];
        y_cf[i * 2 + 1] = 0; // Imaginary part is zero
    }

    ESP_LOGI(TAG, "Signal data stored.");
}

/**
 * @brief Measures the maximum sampling frequency of stored signal data.
 * 
 * This function simulates the sampling process by iterating through the signal data array and counting the number of samples.
 * It calculates the maximum sampling frequency by dividing the total count of samples by the elapsed time (in seconds).
 * This is a simulation which basically determines how fast the ESP32 can sample the signal data if its stored in memory, 
 * which is determined by the smallest delay between samples (portTICK_PERIOD_MS) it can produce.
 * 
 * @note This function assumes that the signal data array is already populated with valid data.
 */
void measure_max_sampling_signal() {
    ESP_LOGW(TAG, "Testing the Max Sampling Frequency by sampling stored signal data...");

    // Obtain the start time in microseconds
    int64_t start_time = esp_timer_get_time();

    // Simulate sampling by iterating through the signal data array and counting the number of samples iterated
    int count = 0;
    for (int i = 0; i < N; i++) {
        // Simulate sampling by reading the data
        float sample = signal_[i];
        count++;
        // Delay by the minimum delay between samples (portTICK_PERIOD_MS) to simulate the maximum sampling frequency
        vTaskDelay(pdMS_TO_TICKS(portTICK_PERIOD_MS));

        // Every 100 samples, log the progress showing the percentage of samples completed
        if (count % 250 == 0) {
            float percent_complete = (float)count / N * 100;
            ESP_LOGI(TAG, "Sampling Progress: %.2f%% complete. Last sample: %.2f", percent_complete, sample);
        }
    }

    // Obtain the end time in microseconds
    int64_t end_time = esp_timer_get_time();

    // Calculate the maximum sampling frequency:
    // Divide the total count of samples by the elapsed time (in seconds) to get the frequency in Hz, remembering that the time is in microseconds
    ESP_LOGW(TAG, "Maximum Sampling Frequency: %d Hz", (int)(count / ((end_time - start_time) / 1000000.0)));
}

/**
 * @brief Finds the peak with the highest frequency on the power spectrum above a certain dB level.
 *
 * This function iterates through the power spectrum array, identifies the peaks above a certain dB level,
 * and returns the frequency of the highest peak.
 *
 * @param db_level The dB level threshold for identifying peaks.
 * @param sampling_frequency The sampling frequency in Hz.
 * @param n_samples The number of samples in the signal.
 * 
 * @return The frequency of the peak with the highest frequency above 0 dB.
 */
float find_highest_frequency_peak_above_db_level(float db_level, float sampling_frequency, int n_samples) {
    float max_value = -100.0;  // Initialize to a very low dB value
    int max_index = -1;

    // Iterate through the power spectrum to find the highest peak above 0 dB
    for (int i = 0; i < N / 2; i++) {
        if (power_spectrum[i] > db_level && power_spectrum[i] > max_value) {
            max_value = power_spectrum[i];
            max_index = i;
        }
    }

    // Calculate the corresponding frequency
    if (max_index != -1) {
        float frequency = (float)max_index * sampling_frequency / n_samples;
        ESP_LOGI(TAG, "Highest frequency peak above %f dB: %f Hz at index %d with value %f dB", db_level, frequency, max_index, max_value);
        return frequency;
    } else {
        ESP_LOGW(TAG, "No peak above %f dB found.", db_level);
        return -1.0;
    }
}

/**
 * Computes the aggregate function over a window.
 *
 * This function calculates the average of the sampled signal over a window of 5 seconds, sampling at
 * 10Hz which is the optimal sampling frequency. It only calculates the average for one window of 5 seconds.
 *
 * @param sampling_frequency The sampling frequency in Hz.
 * @param time_window The number of seconds to sample.
 * @return The average value over the window.
 */
float compute_aggregate(float sampling_frequency, float time_window, signal_function_t signal_func) {
    // Number of samples (output parameter)
    int num_samples;

    // Generate the signal samples with dynamic memory allocation, sampling at the sampling frequency and time window
    float* signal_ = sample_signal_dynamic_with_delay(signal_func, sampling_frequency, time_window, &num_samples);

    // Check if the signal generation failed
    if (signal_ == NULL) {
        ESP_LOGE(TAG, "Signal generation failed.");
        return 0;
    }
    ESP_LOGI(TAG, "Computing aggregate function...");

    // Calculate the sum of the samples by iterating through the signal array obtained from sampling
    float sum = 0;
    int count = 0;
    for (int i = 0; i < num_samples; i++) {
        float sample = signal_[i];
        sum += sample;
        count++;
    }

    // Calculate the average value over the window
    float average = sum / count;
    ESP_LOGI(TAG, "Average value over the window: %f", average);

    // Free the dynamically allocated memory for the signal array
    free(signal_);

    // Return the average value
    return average;
}

/**
 * Publishes a  value to the MQTT broker.
 *
 * This function takes the value as a parameter and publishes it to the MQTT broker under the topic.
 * It creates an AggregationMessage struct, populates its members with the necessary data,
 * converts the struct to JSON format, and publishes it to the topic using the
 * mqtt_publish function.
 *
 * @param value The value value to be published.
 * 
 * @return The amount of bytes sent
 */
size_t publish_data(float value, char* topic, int qos) {
    // Create an AggregationMessage struct
    AggregationMessage aggData;

    //Access members of aggData and assign values to them
    strncpy(aggData.node_id, NODE_ID, 11);
    aggData.node_id[10] = '\0'; // Ensure null termination

    // If value is a very small negative number, such as -0.0000001, set it to 0
    if (value < 0 && value > -0.0001) {
        value = 0;
    }

    // Convert the value to a string with at most 3 digits after the decimal point and store it in the aggregation_result member
    char value_str[11];
    snprintf(value_str, sizeof(value_str), "%.3f", value); // Format with at most 3 decimal digits
    strncpy(aggData.aggregation_result, value_str, 11);
    aggData.aggregation_result[10] = '\0'; // Ensure null termination

    // Publish the authentication data to the /authenticate topic, converting the struct to JSON
    char json[100];  
    snprintf(json, sizeof(json), "{\"node_id\":\"%s\",\"aggregation_result\":\"%s\"}", aggData.node_id, aggData.aggregation_result);


    // Record the start time before publishing for latency measurement
    publish_start_time = esp_timer_get_time();

    mqtt_publish(topic, json, qos, 0);

    // Return the amount of bytes sent in the message
    return strlen(json);
}


/**
 * Publishes energy experiment data to the /energy topic.
 *
 * This function creates an `EnergyMessage` struct, assigns values to its members, and converts the energy values to strings with at most 8 digits after the decimal point. It then publishes the energy data to the /energy topic by converting the struct to JSON format.
 *
 * @param energy_opt The optimal energy value.
 * @param energy_orig The original energy value.
 * @param details Additional details about the energy experiment.
 * @return The amount of bytes sent in the message.
 */
size_t publish_energy_experiment(float energy_opt, float energy_orig, char* details) {
    // Create an EnergyMessage struct
    EnergyMessage energyData;

    //Access members of energyData and assign values to them
    strncpy(energyData.node_id, NODE_ID, 11);
    energyData.node_id[10] = '\0'; // Ensure null termination

    // Convert the energy values to strings with at most 8 digits after the decimal point and store them in the energy_optimal and energy_original members
    char energy_opt_str[11];
    snprintf(energy_opt_str, sizeof(energy_opt_str), "%.7f", energy_opt); // Format with at most 7 decimal digits
    strncpy(energyData.energy_optimal, energy_opt_str, 11);
    energyData.energy_optimal[10] = '\0'; // Ensure null termination

    char energy_orig_str[11];
    snprintf(energy_orig_str, sizeof(energy_orig_str), "%.7f", energy_orig); // Format with at most 7 decimal digits
    strncpy(energyData.energy_original, energy_orig_str, 11);
    energyData.energy_original[10] = '\0'; // Ensure null termination

    // Publish the energy data to the /energy topic, converting the struct to JSON
    char json[512];  
    snprintf(json, sizeof(json), "{\"node_id\":\"%s\",\"energy_optimal\":\"%s\",\"energy_original\":\"%s\",\"details\":\"%s\"}", energyData.node_id, energyData.energy_optimal, energyData.energy_original, details);

    // Publish the energy data to the /energy topic
    mqtt_publish("/energy", json, 0, 0);

    // Return the amount of bytes sent in the message
    return strlen(json);
}


/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_connected = false;
        break;
    case MQTT_EVENT_PUBLISHED:
        // Calculate the latency of the publish event (QoS 1, only gets called on PUBACK)
        int64_t latency = esp_timer_get_time() - publish_start_time;
        ESP_LOGI(TAG, "MQTT: Roundtrip Latency: %lld microseconds", latency);
        measuring_latency = false;
        
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

/**
 * @brief Initializes the MQTT client and starts the MQTT application.
 *
 * This function initializes the MQTT client with the specified configuration and registers the event handler.
 * It then starts the MQTT client.
 */
void mqtt_app_start(void)
{
    // Initialize the MQTT client configuration
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_address,
        .broker.verification.certificate = ca_cert,
        .credentials = {
            .authentication = {
                .certificate = client_cert,
                .key = client_key,
            },
        },
        .network.timeout_ms = 10000,
    };

    // Initialize the MQTT client with the configuration
    client = esp_mqtt_client_init(&mqtt_cfg);
    
    // Register the event handler for the MQTT client
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    // Start the MQTT client
    esp_mqtt_client_start(client);
}

/**
 * @brief Initializes the INA219 library.
 */
void initialize_ina219_library(void)
{
    ESP_ERROR_CHECK(i2cdev_init());

    memset(&dev, 0, sizeof(ina219_t));

    assert(CONFIG_EXAMPLE_SHUNT_RESISTOR_MILLI_OHM > 0);
    ESP_ERROR_CHECK(ina219_init_desc(&dev, I2C_ADDR, I2C_PORT, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_LOGI(TAG, "Initializing INA219");
    ESP_ERROR_CHECK(ina219_init(&dev));

    ESP_LOGI(TAG, "Configuring INA219");
    ESP_ERROR_CHECK(ina219_configure(&dev, INA219_BUS_RANGE_16V, INA219_GAIN_0_125,
            INA219_RES_12BIT_1S, INA219_RES_12BIT_1S, INA219_MODE_CONT_SHUNT_BUS));

    ESP_LOGI(TAG, "Calibrating INA219");

    ESP_ERROR_CHECK(ina219_calibrate(&dev, (float)CONFIG_EXAMPLE_SHUNT_RESISTOR_MILLI_OHM / 1000.0f));
}

/**
 * @brief Task for measuring power values using the INA219 sensor.
 *
 * This task continuously measures power values using the INA219 sensor and stores them in the `pm.power_values` array.
 * It runs until the maximum number of samples is reached or the measurement is stopped.
 *
 * @param pvParameters Pointer to task parameters (not used in this task).
 */

void power_measurement_task(void *pvParameters) {
    ESP_LOGI(TAG, "Power measurement task started");

    // Loop to continuously measure power values
    while (1) {
        // Check if power measurement is active and the maximum number of samples has not been reached
        if (pm.measuring && pm.current_sample_count < pm.max_samples) {
            // Get the power reading from the INA219 sensor
            esp_err_t ret = ina219_get_power(&dev, &power);
            if (ret == ESP_OK) {
                // Store the power value in the power_values array and increment the sample count
                pm.power_values[pm.current_sample_count] = power;
                pm.current_sample_count++;
            } else {
                ESP_LOGE(TAG, "Failed to get power reading: %s", esp_err_to_name(ret));
            }
            vTaskDelay(pdMS_TO_TICKS(10));  // Measure every 10ms
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));  // Sleep for 100ms if not measuring
        }
    }
}

/**
 * @brief Starts the power measurement for a specified duration.
 *
 * This function starts the power measurement for a specified duration in seconds.
 * It allocates memory for storing power values and initializes/resets the necessary variables.
 * The power measurement is performed using a timer, and the start time is recorded.
 *
 * @param max_duration_seconds The maximum duration of the power measurement in seconds.
 */
void start_power_measurement(int max_duration_seconds) {
    // Allocate memory for storing power values based on the maximum duration
    pm.max_samples = (max_duration_seconds * 1000) / 10;
    pm.power_values = (float *)malloc(pm.max_samples * sizeof(float));
    if (pm.power_values == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for power measurements");
    }

    // Reset the current sample count and set the measuring flag to true
    pm.current_sample_count = 0;
    pm.measuring = true;

    // Start the timer for power measurement
    pm.start_time = esp_timer_get_time();    
}

/**
 * @brief Stops the power measurement and calculates the average power and total energy.
 *
 * This function stops the power measurement, calculates the elapsed time, average power, and total energy
 * based on the collected power values. It logs the measurement results and frees the memory allocated for
 * the power values array.
 *
 * @return A structure containing the average power in milliwatts and total energy in watt-hours.
 */
power_measurement_result_t end_power_measurement() {
    // Stop the power measurement after the function completes
    pm.measuring = false;

    // End the timer for power measurement
    pm.end_time = esp_timer_get_time();

    // Calculate the elapsed time in seconds
    float elapsed_time_s = (pm.end_time - pm.start_time) / 1000000.0;

    ESP_LOGI(TAG, "Power measurement complete. Samples: %d, Elapsed Time: %f s", pm.current_sample_count, elapsed_time_s);

    // Calculate average power and total energy
    float total_power = 0;
    for (int i = 0; i < pm.current_sample_count; i++) {
        total_power += pm.power_values[i];
    }
    float average_power = total_power / pm.current_sample_count;
    float total_energy_joules = (total_power / pm.current_sample_count) * elapsed_time_s;  // Energy in joules (power in mW * time in seconds)
    float total_energy_wh = total_energy_joules / 3600.0;    // Energy in watt-hours (Wh)

    ESP_LOGI(TAG, "Average Power: %f mW", average_power);
    ESP_LOGI(TAG, "Total Energy: %f Wh", total_energy_wh);

    // Free the memory allocated for power values
    free(pm.power_values);

    // Return the average power and total energy in the result structure
    return (power_measurement_result_t){.average_power = average_power, .total_energy_wh = total_energy_wh};
}

/**
 * @brief Runs the bonus experiment to measure energy savings
 *
 * This function performs the following steps:
 * 1. Calculates the power spectrum of the input signal using FFT processing.
 * 2. Finds the peak frequency on the power spectrum above 0 dB.
 * 3. Computes the optimal sampling frequency based on the highest frequency peak.
 * 4. Runs the energy savings measurement experiment:
 *    a. Computes the average value of the signal using the optimal sampling frequency in a window of 5 seconds.
 *    b. Publishes the average value to the MQTT broker.
 *    c. Measures the average power and total energy consumption for the optimal sampling frequency.
 *    d. Computes the average value of the signal using the original sampling frequency in a window of 5 seconds.
 *    e. Publishes the average value to the MQTT broker.
 *    f. Measures the average power and total energy consumption for the original sampling frequency.
 *    g. Publishes the energy consumption data to the /energy topic.
 * Noting that the computation of the average and publish is done 5 times for each sampling frequency.
 *
 * @param signal_func The input signal function.
 * @param original_sampling_rate The original sampling rate of the input signal.
 * @param time_window The time window for computing the average value.
 */
void bonus_run_experiment(signal_function_t signal_func, int original_sampling_rate, int time_window) {
    // Store the signal data in memory (with a Hann window applied)
    store_signal(signal_func, original_sampling_rate);

    // FFT processing to find the power spectrum, as seen in the official ESP-DSP example:
    dsps_fft2r_fc32(y_cf, N);
    // Bit reverse
    dsps_bit_rev_fc32(y_cf, N);
    // Convert one complex vector to two complex vectors
    dsps_cplx2reC_fc32(y_cf, N);

    // Calculate power spectrum
    for (int i = 0; i < N; i++) {
        power_spectrum[i] = 10 * log10f((y_cf[i * 2] * y_cf[i * 2] + y_cf[i * 2 + 1] * y_cf[i * 2 + 1]) / N);
    }

    // Find the peak with the highest frequency on the power spectrum above 0 dB
    float highest_frequency_peak = find_highest_frequency_peak_above_db_level(0.0, original_sampling_rate, N_SAMPLES);
    float optimal_sampling_frequency = highest_frequency_peak * 2;
    ESP_LOGW(TAG, "Maximum Frequency of the Signal is %f Hz. Optimal Sampling Frequency: %f Hz", highest_frequency_peak, optimal_sampling_frequency);
    
    // Since we are using Tumbling Window, it doesn't make sense to run the volume of data experiment, neither for the Latency since
    // the sampling is not expected to change the timing between the publish and the receive of the message by the edge server.

    // Run the energy savings measurement experiment:
    // 1. Run the aggregate and publish 5 times, using the optimal sampling frequency of 10Hz in a window of 5 seconds. After the entire
    // process is finished, send over the energy consumption data to the /energy topic.
    if (power_measurement_active) {

        // Start the power measurement for the optimal sampling frequency
        start_power_measurement(time_window*3*5);

        for (int i = 0; i < 5; i++) {
            // Compute the aggregate function (average) using the optimal sampling frequency of 10Hz in a window of 5 seconds
            float average = compute_aggregate(optimal_sampling_frequency, time_window, signal_func);

            // Publish the average value to the MQTT broker
            publish_data(average, "/average", 0);
        }

        // End the power measurement and get the results
        power_measurement_result_t result_optimal = end_power_measurement();

        ESP_LOGW(TAG, "Optimal Sampling Frequency: Measured Average Power: %f mW", result_optimal.average_power);
        ESP_LOGW(TAG, "Optimal Sampling Frequency: Measured Total Energy: %f Wh", result_optimal.total_energy_wh);


        // 2. Run the aggregate and publish 10 times, using the original sampling frequency of 100Hz in a window of 5 seconds.

        // Start the power measurement for the original sampling frequency
        start_power_measurement(time_window*3*5);

        for (int i = 0; i < 5; i++) {
            // Compute the aggregate function (average) using the original sampling frequency of 100Hz in a window of 5 seconds
            float average = compute_aggregate(original_sampling_rate, time_window, signal_func);

            // Publish the average value to the MQTT broker using the original sampling frequency
            publish_data(average, "/average", 0);
        }

        // End the power measurement and get the results
        power_measurement_result_t result_original = end_power_measurement();

        ESP_LOGW(TAG, "Original Sampling Frequency: Measured Average Power: %f mW", result_original.average_power);
        ESP_LOGW(TAG, "Original Sampling Frequency: Measured Total Energy: %f Wh", result_original.total_energy_wh);

        // 3. Publish the energy consumption data to the /energy topic
        // Initialize a string to store the details of the experiment
        char details[256];

        // On the details, include the information about the original and optimal sampling frequencies
        snprintf(details, sizeof(details), "EXPERIMENT: Original Sampling Freq: %d Hz, Optimal Sampling Freq: %d Hz, Time Window: %d", original_sampling_rate, (int)optimal_sampling_frequency, time_window);

        // Publish the energy consumption data to the /energy topic
        publish_energy_experiment(result_optimal.total_energy_wh, result_original.total_energy_wh, details);
    }
    
}

// Main function
void app_main(void) {

    // ********** 1. SETUP **********
    // Initialize NVS for storing wifi credentials and mqtt config
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Connect to wifi
    wifi_connection();
    // Delay to let it connect to wifi before starting MQTT
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    // Start MQTT
    mqtt_app_start();
    // Delay to let it connect to MQTT before proceeding
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // Initialize FFT
    ret = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Not possible to initialize FFT. Error = %i", ret);
    }

    // Initialize INA219 library (Following the INA219 esp-idf-lib example: https://github.com/UncleRus/esp-idf-lib/blob/master/examples/ina219/default/main/main.c)
    if (power_measurement_active) {
        initialize_ina219_library();
    }
    
    // ********** 2. INPUT SIGNAL **********

    // Store the signal data in memory (with a Hann window applied)
    store_signal(input_signal_1, SIGNAL_ORIGINAL_SAMPLING_FREQUENCY);
    ESP_LOGI(TAG, "Signal storage complete. Proceeding to sampling...");


    // ********** 3. MAXIMUM SAMPLING FREQUENCY **********

    // Measure the maximum sampling frequency of the stored signal data, simulating a sampling process that reads the data from memory
    // and counts the number of samples iterated and the elapsed time. Since the signal data is already stored in memory, this function
    // simulates how fast the ESP32 can sample the signal data, i.e., how many samples it can read in a given time window from memory
    measure_max_sampling_signal();

    // ********** 4. FFT / IDENTIFY OPTIMAL SAMPLING FREQUENCY **********

    // FFT processing to find the power spectrum, as seen in the official ESP-DSP example:
    // https://github.com/espressif/esp-dsp/blob/master/examples/fft/README.md
    unsigned int start_b = dsp_get_cpu_cycle_count();
    dsps_fft2r_fc32(y_cf, N);
    unsigned int end_b = dsp_get_cpu_cycle_count();
    // Bit reverse
    dsps_bit_rev_fc32(y_cf, N);
    // Convert one complex vector to two complex vectors
    dsps_cplx2reC_fc32(y_cf, N);

    // Calculate power spectrum
    for (int i = 0; i < N; i++) {
        power_spectrum[i] = 10 * log10f((y_cf[i * 2] * y_cf[i * 2] + y_cf[i * 2 + 1] * y_cf[i * 2 + 1]) / N);
    }

    // Show power spectrum in 100x15 window from -100 to 20 dB from 0..N/2 samples
    ESP_LOGW(TAG, "Power Spectrum");
    dsps_view(power_spectrum, N / 2, 100, 12, -60, 60, '|');
    ESP_LOGI(TAG, "FFT for %i complex points take %i cycles", N, end_b - start_b);

    // Find the peak with the highest frequency on the power spectrum above 0 dB
    float highest_frequency_peak = find_highest_frequency_peak_above_db_level(0.0, SIGNAL_ORIGINAL_SAMPLING_FREQUENCY, N_SAMPLES);
    
    // After running the program once, we can see that the maximum value of the power spectrum is 41.917538, which is at index 205.
    // Since we have 4096 power spectrum components, and the sampling frequency is 100Hz, each component is 100/4096 = 0.0244Hz. 
    // The maximum frequency of our signal is 0.0244 * 205 = 5Hz (corresponding to the sinusoid with the highest frequency in the sum of sinusoids). 
    // The optimal sampling frequency is then 10Hz, following the Nyquist theorem.
    float optimal_sampling_frequency = highest_frequency_peak * 2;
    ESP_LOGW(TAG, "Maximum Frequency of the Signal is %f Hz. Optimal Sampling Frequency: %f Hz", highest_frequency_peak, optimal_sampling_frequency);
    
    // ********** 5. AGGREGATE FUNCTION **********

    // Compute the aggregate function (average) using the optimal sampling frequency of 10Hz in a window of 5 seconds
    float average = compute_aggregate(optimal_sampling_frequency, 5, input_signal_1);

    // ********** 6. COMMUNICATION TO EDGE SERVER ********** 

    // Publish the average value to the MQTT broker
    publish_data(average, "/average", 0);


    // ********** 7. PERFORMANCE REPORT **********

    // *** 7.1. Energy Savings Measurement:
    if (power_measurement_active) {
        // Create the power measurement task
        xTaskCreate(power_measurement_task, "power_measurement_task", 4096, NULL, 5, NULL);


        // 1. Run the aggregate and publish 5 times, using the optimal sampling frequency of 10Hz in a window of 5 seconds. After the entire
        // process is finished, send over the energy consumption data to the /energy topic.
        start_power_measurement(100);

        for (int i = 0; i < 5; i++) {
            // Compute the aggregate function (average) using the optimal sampling frequency of 10Hz in a window of 5 seconds
            average = compute_aggregate(optimal_sampling_frequency, 5, input_signal_1);

            // Publish the average value to the MQTT broker
            publish_data(average, "/average", 0);
        }

        power_measurement_result_t result = end_power_measurement();

        ESP_LOGW(TAG, "Optimal Sampling Frequency: Measured Average Power: %f mW", result.average_power);
        ESP_LOGW(TAG, "Optimal Sampling Frequency: Measured Total Energy: %f Wh", result.total_energy_wh);


        // 2. Run the aggregate and publish 10 times, using the original sampling frequency of 100Hz in a window of 5 seconds.
        start_power_measurement(100);

        for (int i = 0; i < 5; i++) {
            // Compute the aggregate function (average) using the original sampling frequency of 100Hz in a window of 5 seconds
            average = compute_aggregate(SIGNAL_ORIGINAL_SAMPLING_FREQUENCY, 5, input_signal_1);

            // Publish the average value to the MQTT broker using the original sampling frequency
            publish_data(average, "/average", 0);
        }

        power_measurement_result_t result_100Hz = end_power_measurement();

        ESP_LOGW(TAG, "Original Sampling Frequency: Measured Average Power: %f mW", result_100Hz.average_power);
        ESP_LOGW(TAG, "Original Sampling Frequency: Measured Total Energy: %f Wh", result_100Hz.total_energy_wh);

        // Transmit the energy savings data to the /energy topic
        publish_energy_experiment(result.total_energy_wh, result_100Hz.total_energy_wh, "Energy Savings Measurement for 10Hz vs 100Hz Sampling Frequency in a 5-second Window.");

    }
    
    // *** 7.2. Volume of Data Measurement:

    // 1. Optimal Sampling Frequency: 10Hz
    // Compute the aggregate function (average) using the optimal sampling frequency of 10Hz in a window of 5 seconds
    average = compute_aggregate(optimal_sampling_frequency, 5, input_signal_1);

    // Publish the average value to the MQTT broker
    size_t bytes_message_10Hz = publish_data(average, "/average", 0);

    // 2. Original Sampling Frequency: 100Hz

    // Compute the aggregate function (average) using the original sampling frequency of 100Hz in a window of 5 seconds
    average = compute_aggregate(SIGNAL_ORIGINAL_SAMPLING_FREQUENCY, 5, input_signal_1);

    // Publish the average value to the MQTT broker using the original sampling frequency
    size_t bytes_message_100Hz = publish_data(average, "/average", 0);

    // Log the volume of data sent for both sampling frequencies
    ESP_LOGW(TAG, "Volume of Data Sent for Optimal Sampling Frequency (10Hz): %zu bytes", bytes_message_10Hz);
    ESP_LOGW(TAG, "Volume of Data Sent for Original Sampling Frequency (100Hz): %zu bytes", bytes_message_100Hz);
    
    // *** 7.3. Latency Measurement:
    
    //Run publish 10 times and measure the latency of each publish event
    ESP_LOGW(TAG, "Latency Measurement: Running publish 10 times and measuring the latency of each publish event...");
    for (int i = 0; i < 10; i++) {
        // Publish the average value to the MQTT broker
        measuring_latency = true;
        publish_data(average, "/average", 1);
        while (measuring_latency) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    // ********** 8. BONUS **********
    ESP_LOGW(TAG, "Running Bonus Experiments...");

    // Run the bonus experiment with the input signal 1
    ESP_LOGW(TAG, "Running Bonus Experiment with Input Signal 1...");
    bonus_run_experiment(input_signal_1, 500, 5);

    // Run the bonus experiment with the input signal 2
    ESP_LOGW(TAG, "Running Bonus Experiment with Input Signal 2...");
    bonus_run_experiment(input_signal_2, 500, 5);

    // Run the bonus experiment with the input signal 3
    ESP_LOGW(TAG, "Running Bonus Experiment with Input Signal 3...");
    bonus_run_experiment(input_signal_3, 500, 5);


    while (1) {
        // Delay to keep the task running
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    
}

