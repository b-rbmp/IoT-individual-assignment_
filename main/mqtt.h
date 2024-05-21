// A struct used for passing the JSON message to the /aggregate topic
typedef struct {
    char node_id[11];       // UID with space for null-terminator
    char aggregation_result[11];        // Aggregation result with space for null-terminator
} AggregationMessage;
typedef struct {
    char node_id[11];       // UID with space for null-terminator
    char energy_optimal[11];        // Energy optimal with space for null-terminator
    char energy_original[11];        // Energy current with space for null-terminator
    char details[256];        // Details with space for null-terminator (max 256 characters)
} EnergyMessage;

void mqtt_app_start(void);