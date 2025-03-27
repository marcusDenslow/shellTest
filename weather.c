/**
 * weather.c
 * Implementation of weather information fetching and display
 */

#include "weather.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wininet.h>

// Define constants for API requests
#define BUFFER_SIZE 8192
#define IP_API_HOST "ipapi.co"
#define IP_API_PATH "/json/"
#define WEATHER_API_HOST "api.openweathermap.org"
#define WEATHER_API_PATH "/data/2.5/weather"

// Define a placeholder for the weather API key
#define WEATHER_API_KEY_PLACEHOLDER "YOUR_API_KEY_HERE"

// Global debug flag
static BOOL g_debug_mode = FALSE;

// Structure to hold location data
typedef struct {
  char city[64];
  char region[64];
  char country[64];
  char latitude[16];
  char longitude[16];
} LocationData;

// Structure to hold weather data
typedef struct {
  char temperature[16];
  char feels_like[16];
  char humidity[16];
  char description[64];
  char wind_speed[16];
  char wind_direction[16];
  char pressure[16];
  char icon[16];
  char city[64];    // Added for complete location info
  char region[64];  // Added for complete location info
  char country[64]; // Added for complete location info
} WeatherData;

// Debug print function that only outputs when debug mode is enabled
static void debug_print(const char *format, ...) {
  if (!g_debug_mode)
    return;

  va_list args;
  va_start(args, format);
  printf("Debug: ");
  vprintf(format, args);
  va_end(args);
}

// Function to read API key from config file
static char *read_api_key_from_config() {
  static char api_key[64] = "";

  // Try to locate config file in user's home directory
  char config_path[MAX_PATH];
  char *home_dir = getenv("USERPROFILE");

  if (home_dir) {
    snprintf(config_path, sizeof(config_path), "%s\\.lsh_weather_config",
             home_dir);
  } else {
    // Fallback to current directory
    strcpy(config_path, ".lsh_weather_config");
  }

  // Try to open the config file
  FILE *config_file = fopen(config_path, "r");
  if (!config_file) {
    return NULL;
  }

  // Read the API key (first line of the file)
  if (fgets(api_key, sizeof(api_key), config_file)) {
    // Remove newline if present
    size_t len = strlen(api_key);
    if (len > 0 && api_key[len - 1] == '\n') {
      api_key[len - 1] = '\0';
    }

    // Check if key is valid (not empty)
    if (strlen(api_key) > 0) {
      fclose(config_file);
      return api_key;
    }
  }

  fclose(config_file);
  return NULL;
}

// Function to create a sample config file
static void create_sample_config_file() {
  char config_path[MAX_PATH];
  char *home_dir = getenv("USERPROFILE");

  if (home_dir) {
    snprintf(config_path, sizeof(config_path), "%s\\.lsh_weather_config",
             home_dir);
  } else {
    // Fallback to current directory
    strcpy(config_path, ".lsh_weather_config");
  }

  FILE *config_file = fopen(config_path, "w");
  if (!config_file) {
    printf("Could not create sample config file at: %s\n", config_path);
    return;
  }

  fprintf(config_file, "YOUR_API_KEY_HERE\n");
  fprintf(config_file,
          "# Replace the line above with your actual OpenWeatherMap API key\n");
  fprintf(config_file,
          "# Get a free API key at: https://openweathermap.org/\n");

  fclose(config_file);
  printf("Created sample config file at: %s\n", config_path);
  printf(
      "Please edit this file and replace the first line with your API key.\n");
}

static const char *get_weather_api_key() {
  static char *api_key = NULL;

  // If we've already loaded the key, return it
  if (api_key)
    return api_key;

  // Try to read from config file
  api_key = read_api_key_from_config();

  // Return the key if we found it
  if (api_key)
    return api_key;

  // If no key found, create a sample config and return placeholder
  create_sample_config_file();
  return WEATHER_API_KEY_PLACEHOLDER;
}

// Forward declarations for internal functions
static int get_location_by_ip(LocationData *location);
static int get_weather_data(const char *location, WeatherData *weather);
static void display_weather(const LocationData *location,
                            const WeatherData *weather);
static char *extract_json_string(const char *json, const char *key);
static double extract_json_number(const char *json, const char *key);
static char *http_get_request(const char *host, const char *path,
                              const char *query_params);

/**
 * Update the LocationData structure with weather API response data
 * This ensures we capture city, state/region, and country even when searching
 * by city name
 */
static void update_location_from_weather(LocationData *location,
                                         const char *response) {
  // Get city name from response (might be more accurate than our search query)
  char *name = extract_json_string(response, "name");
  if (name) {
    if (location->city[0] == '\0' || strcmp(location->city, name) != 0) {
      // Only update if empty or different from what we already have
      strncpy(location->city, name, sizeof(location->city) - 1);
      location->city[sizeof(location->city) - 1] = '\0';
    }
    free(name);
  }

  // Get country from the "sys" object
  char *sys_json = strstr(response, "\"sys\":");
  if (sys_json) {
    // Country code
    char *country_code = extract_json_string(sys_json, "country");
    if (country_code) {
      // Convert country code to full name if empty
      if (location->country[0] == '\0') {
        // Map some common country codes
        if (strcmp(country_code, "US") == 0)
          strcpy(location->country, "United States");
        else if (strcmp(country_code, "GB") == 0)
          strcpy(location->country, "United Kingdom");
        else if (strcmp(country_code, "CA") == 0)
          strcpy(location->country, "Canada");
        else if (strcmp(country_code, "AU") == 0)
          strcpy(location->country, "Australia");
        else if (strcmp(country_code, "MX") == 0)
          strcpy(location->country, "Mexico");
        else
          strcpy(location->country, country_code); // Use code if no mapping
      }
      free(country_code);
    }
  }
}

/**
 * Command handler for the "weather" command
 */
int lsh_weather(char **args) {
  // Reset debug mode by default
  g_debug_mode = FALSE;

  // Check for debug flag
  int arg_start = 1;
  if (args[1] != NULL &&
      (strcmp(args[1], "-d") == 0 || strcmp(args[1], "-debug") == 0)) {
    g_debug_mode = TRUE;
    arg_start = 2;
    debug_print("Debug mode enabled\n");
  }

  // Check if API key is properly configured
  const char *api_key = get_weather_api_key();
  if (strcmp(api_key, WEATHER_API_KEY_PLACEHOLDER) == 0) {
    printf("Weather API key not configured.\n");
    printf("Please edit the config file at: %s\\.lsh_weather_config\n",
           getenv("USERPROFILE"));
    printf("And replace the first line with your OpenWeatherMap API key.\n");
    return 1;
  }

  LocationData location;
  WeatherData weather;
  int success = 0;

  // Clear structures
  memset(&location, 0, sizeof(LocationData));
  memset(&weather, 0, sizeof(WeatherData));

  // Check if a location was provided
  if (args[arg_start] != NULL) {
    // Handle multi-word city names by concatenating all arguments
    char full_location[256] = "";
    int i = arg_start;

    // Start with the first word
    strcpy(full_location, args[i]);

    // Add remaining words with spaces in between
    i++;
    while (args[i] != NULL) {
      strcat(full_location, " ");
      strcat(full_location, args[i]);
      i++;
    }

    // Use the full location string
    strcpy(location.city, full_location);
    success = get_weather_data(location.city, &weather);
  } else {
    // Try to detect the user's location by IP
    if (get_location_by_ip(&location)) {
      success = get_weather_data(location.city, &weather);
    } else {
      printf("Failed to detect your location. Please provide a location: "
             "weather <city>\n");
      return 1;
    }
  }

  if (success) {
    display_weather(&location, &weather);
  } else {
    printf("Failed to retrieve weather data. Please check your connection or "
           "try a different location.\n");
  }

  return 1;
}

/**
 * Get the user's location based on their IP address
 *
 * @param location Pointer to LocationData structure to store the results
 * @return 1 if successful, 0 on failure
 */
static int get_location_by_ip(LocationData *location) {
  char *response = http_get_request(IP_API_HOST, IP_API_PATH, NULL);
  if (!response) {
    debug_print("Failed to get location by IP - no response\n");
    return 0;
  }

  // Extract location information from the JSON response
  char *city = extract_json_string(response, "city");
  char *region = extract_json_string(response, "region");
  char *country = extract_json_string(response, "country_name");
  char *latitude = extract_json_string(response, "latitude");
  char *longitude = extract_json_string(response, "longitude");

  if (city) {
    strncpy(location->city, city, sizeof(location->city) - 1);
    free(city);
  }

  if (region) {
    strncpy(location->region, region, sizeof(location->region) - 1);
    free(region);
  }

  if (country) {
    strncpy(location->country, country, sizeof(location->country) - 1);
    free(country);
  }

  if (latitude) {
    strncpy(location->latitude, latitude, sizeof(location->latitude) - 1);
    free(latitude);
  }

  if (longitude) {
    strncpy(location->longitude, longitude, sizeof(location->longitude) - 1);
    free(longitude);
  }

  free(response);

  // Check if we got at least the city
  if (location->city[0] == '\0') {
    debug_print("Failed to extract city from location response\n");
    return 0;
  }

  debug_print("Successfully detected location: %s, %s, %s\n", location->city,
              location->region[0] ? location->region : "N/A",
              location->country[0] ? location->country : "N/A");

  return 1;
}

/**
 * Get weather data for a specific location
 *
 * @param location The location to get weather for
 * @param weather Pointer to WeatherData structure to store the results
 * @return 1 if successful, 0 on failure
 */
static int get_weather_data(const char *location, WeatherData *weather) {
  char query_params[256];
  snprintf(query_params, sizeof(query_params), "?q=%s&appid=%s&units=metric",
           location, get_weather_api_key());

  printf("Attempting to get weather for: %s\n", location);
  debug_print("Using API endpoint: %s\n", WEATHER_API_PATH, query_params);

  char *response =
      http_get_request(WEATHER_API_HOST, WEATHER_API_PATH, query_params);
  if (!response) {
    debug_print("HTTP request failed - no response received\n");
    return 0;
  }

  // Check if response contains an error message
  if (strstr(response, "\"cod\":\"404\"")) {
    debug_print("Location not found (404): %s\n", location);
    free(response);
    return 0;
  }

  if (strstr(response, "\"cod\":\"401\"")) {
    debug_print("API key error (401) - Invalid API key or not activated yet\n");
    free(response);
    return 0;
  }

  debug_print("Received API response of length: %d bytes\n",
              (int)strlen(response));

  // Extract weather information from the JSON response
  char *main_json = strstr(response, "\"main\":");
  if (!main_json) {
    debug_print("Could not find 'main' section in response\n");
    free(response);
    return 0;
  }

  // Extract temperature, feels_like, humidity, pressure
  double temp = extract_json_number(main_json, "temp");
  double feels_like = extract_json_number(main_json, "feels_like");
  double humidity = extract_json_number(main_json, "humidity");
  double pressure = extract_json_number(main_json, "pressure");

  snprintf(weather->temperature, sizeof(weather->temperature), "%.1f°C", temp);
  snprintf(weather->feels_like, sizeof(weather->feels_like), "%.1f°C",
           feels_like);
  snprintf(weather->humidity, sizeof(weather->humidity), "%.0f%%", humidity);
  snprintf(weather->pressure, sizeof(weather->pressure), "%.0f hPa", pressure);

  // Wind data is in the "wind" object
  char *wind_json = strstr(response, "\"wind\":");
  if (wind_json) {
    double speed = extract_json_number(wind_json, "speed");
    double deg = extract_json_number(wind_json, "deg");

    snprintf(weather->wind_speed, sizeof(weather->wind_speed), "%.1f m/s",
             speed);

    // Convert wind degrees to direction
    const char *directions[] = {"N",  "NNE", "NE", "ENE", "E",  "ESE",
                                "SE", "SSE", "S",  "SSW", "SW", "WSW",
                                "W",  "WNW", "NW", "NNW"};
    int dir_index = (int)((deg + 11.25) / 22.5) % 16;
    snprintf(weather->wind_direction, sizeof(weather->wind_direction), "%s",
             directions[dir_index]);
  } else {
    debug_print("Could not find 'wind' section in response\n");
  }

  // Weather description is in the "weather" array's first object
  char *weather_json = strstr(response, "\"weather\":[");
  if (weather_json) {
    char *desc = extract_json_string(weather_json, "description");
    char *icon = extract_json_string(weather_json, "icon");

    if (desc) {
      // Capitalize first letter
      if (desc[0] != '\0') {
        desc[0] = toupper(desc[0]);
      }
      strncpy(weather->description, desc, sizeof(weather->description) - 1);
      free(desc);
    } else {
      debug_print("Could not extract 'description' from response\n");
    }

    if (icon) {
      strncpy(weather->icon, icon, sizeof(weather->icon) - 1);
      free(icon);
    } else {
      debug_print("Could not extract 'icon' from response\n");
    }
  } else {
    debug_print("Could not find 'weather' section in response\n");
  }

  // Extract location data from the response
  LocationData loc_temp;
  memset(&loc_temp, 0, sizeof(LocationData));
  update_location_from_weather(&loc_temp, response);

  // Copy location data to weather struct for display
  strncpy(weather->city, loc_temp.city[0] ? loc_temp.city : location,
          sizeof(weather->city) - 1);
  weather->city[sizeof(weather->city) - 1] = '\0';

  strncpy(weather->region, loc_temp.region, sizeof(weather->region) - 1);
  weather->region[sizeof(weather->region) - 1] = '\0';

  strncpy(weather->country, loc_temp.country, sizeof(weather->country) - 1);
  weather->country[sizeof(weather->country) - 1] = '\0';

  debug_print("Successfully parsed weather data\n");
  debug_print("Hey there!\n");
  return 1;
}

/**
 * Display weather information in a pretty box with centered white text
 */
static void display_weather(const LocationData *location,
                            const WeatherData *weather) {
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  WORD originalAttrs;

  // Get original console attributes
  CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
  GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
  originalAttrs = consoleInfo.wAttributes;

  // Set box color to cyan
  WORD boxColor = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED |
                  FOREGROUND_INTENSITY;

  // Set text color to bright white
  WORD textColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                   FOREGROUND_INTENSITY;

  // Calculate box dimensions
  int boxWidth = 52;
  int contentWidth = boxWidth - 4; // 2 chars padding on each side

  // Get console width for centering
  int consoleWidth = consoleInfo.dwSize.X;
  if (consoleWidth <= 0) {
    consoleWidth = 80; // Default if we can't get the console width
  }

  // Calculate left padding to center the box on the screen
  int left_padding = (consoleWidth - boxWidth) / 2;
  if (left_padding < 0)
    left_padding = 0;

  // Construct location string - prioritize complete location data
  char locationStr[128] = "";

  // Use weather struct for location data if available (more accurate from
  // search)
  if (weather->city[0] != '\0') {
    strcat(locationStr, weather->city);
  } else if (location->city[0] != '\0') {
    strcat(locationStr, location->city);
  }

  if (weather->region[0] != '\0') {
    if (locationStr[0] != '\0')
      strcat(locationStr, ", ");
    strcat(locationStr, weather->region);
  } else if (location->region[0] != '\0') {
    if (locationStr[0] != '\0')
      strcat(locationStr, ", ");
    strcat(locationStr, location->region);
  }

  if (weather->country[0] != '\0') {
    if (locationStr[0] != '\0')
      strcat(locationStr, ", ");
    strcat(locationStr, weather->country);
  } else if (location->country[0] != '\0') {
    if (locationStr[0] != '\0')
      strcat(locationStr, ", ");
    strcat(locationStr, location->country);
  }

  // Create weather icon based on condition
  const char *icon = "☀️"; // Default to sun

  // Map OpenWeatherMap icon codes to Unicode symbols
  if (weather->icon[0] != '\0') {
    char icon_code[3] = {weather->icon[0], weather->icon[1], '\0'};

    if (strcmp(icon_code, "01") == 0)
      icon = "☀️"; // Clear sky
    else if (strcmp(icon_code, "02") == 0)
      icon = "🌤️"; // Few clouds
    else if (strcmp(icon_code, "03") == 0)
      icon = "☁️"; // Scattered clouds
    else if (strcmp(icon_code, "04") == 0)
      icon = "☁️"; // Broken clouds
    else if (strcmp(icon_code, "09") == 0)
      icon = "🌧️"; // Shower rain
    else if (strcmp(icon_code, "10") == 0)
      icon = "🌦️"; // Rain
    else if (strcmp(icon_code, "11") == 0)
      icon = "⛈️"; // Thunderstorm
    else if (strcmp(icon_code, "13") == 0)
      icon = "❄️"; // Snow
    else if (strcmp(icon_code, "50") == 0)
      icon = "🌫️"; // Mist
  }

  // Draw the box with weather info
  printf("\n");

  // Top border
  SetConsoleTextAttribute(hConsole, boxColor);
  printf("%*s", left_padding, "");
  printf("╔══════════════════════════════════════════════════╗\n");

  // Title row - centered
  printf("%*s", left_padding, "");
  printf("║");
  SetConsoleTextAttribute(hConsole, textColor);

  const char *title = "CURRENT WEATHER";
  int titleLen = strlen(title);
  int titlePadding = (contentWidth - titleLen) / 2;
  printf("%*s%s%*s", titlePadding + 2, "", title,
         contentWidth - titleLen - titlePadding, "");

  SetConsoleTextAttribute(hConsole, boxColor);
  printf("║\n");

  // Location row - left-aligned
  printf("%*s", left_padding, "");
  printf("║");
  SetConsoleTextAttribute(hConsole, textColor);
  printf("  %-*s", contentWidth, locationStr);
  SetConsoleTextAttribute(hConsole, boxColor);
  printf("║\n");

  // Separator
  printf("%*s", left_padding, "");
  printf("╠══════════════════════════════════════════════════╣\n");

  // Temperature row - left-aligned
  char tempDisplay[32];
  sprintf(tempDisplay, "%s  %s", icon, weather->temperature);

  printf("%*s", left_padding, "");
  printf("║");
  SetConsoleTextAttribute(hConsole, textColor);
  printf("  %-*s", contentWidth, tempDisplay);
  SetConsoleTextAttribute(hConsole, boxColor);
  printf("     ║\n");

  // Description row - left-aligned
  printf("%*s", left_padding, "");
  printf("║");
  SetConsoleTextAttribute(hConsole, textColor);
  printf("  %-*s", contentWidth, weather->description);
  SetConsoleTextAttribute(hConsole, boxColor);
  printf("║\n");

  // Feels like row - left-aligned
  char feelsLikeText[64];
  sprintf(feelsLikeText, "Feels like: %s", weather->feels_like);

  printf("%*s", left_padding, "");
  printf("║");
  SetConsoleTextAttribute(hConsole, textColor);
  printf("  %-*s", contentWidth, feelsLikeText);
  SetConsoleTextAttribute(hConsole, boxColor);
  printf(" ║\n");

  // Separator
  printf("%*s", left_padding, "");
  printf("╠══════════════════════════════════════════════════╣\n");

  // Details rows - left-aligned
  char humidityText[64];
  sprintf(humidityText, "Humidity: %s", weather->humidity);

  printf("%*s", left_padding, "");
  printf("║");
  SetConsoleTextAttribute(hConsole, textColor);
  printf("  %-*s", contentWidth, humidityText);
  SetConsoleTextAttribute(hConsole, boxColor);
  printf("║\n");

  // Wind row - left-aligned
  char windText[64];
  sprintf(windText, "Wind: %s",
          weather->wind_speed[0] != '\0' ? weather->wind_speed : "N/A");

  printf("%*s", left_padding, "");
  printf("║");
  SetConsoleTextAttribute(hConsole, textColor);
  printf("  %-*s", contentWidth, windText);
  SetConsoleTextAttribute(hConsole, boxColor);
  printf("║\n");

  // Direction row - left-aligned
  if (weather->wind_direction[0] != '\0') {
    char directionText[64];
    sprintf(directionText, "Direction: %s", weather->wind_direction);

    printf("%*s", left_padding, "");
    printf("║");
    SetConsoleTextAttribute(hConsole, textColor);
    printf("  %-*s", contentWidth, directionText);
    SetConsoleTextAttribute(hConsole, boxColor);
    printf("║\n");
  }

  // Pressure row - left-aligned
  char pressureText[64];
  sprintf(pressureText, "Pressure: %s", weather->pressure);

  printf("%*s", left_padding, "");
  printf("║");
  SetConsoleTextAttribute(hConsole, textColor);
  printf("  %-*s", contentWidth, pressureText);
  SetConsoleTextAttribute(hConsole, boxColor);
  printf("║\n");

  // Bottom border
  printf("%*s", left_padding, "");
  printf("╚══════════════════════════════════════════════════╝\n\n");

  // Reset console attributes
  SetConsoleTextAttribute(hConsole, originalAttrs);
}

/**
 * Extract a string value from a JSON object
 */
static char *extract_json_string(const char *json, const char *key) {
  char search_key[256];
  char *value = NULL;
  char *key_pos, *value_start, *value_end;
  int value_len;

  // Format the key with quotes
  snprintf(search_key, sizeof(search_key), "\"%s\":", key);

  // Find the key in the JSON
  key_pos = strstr(json, search_key);
  if (!key_pos)
    return NULL;

  // Move past the key and colon
  key_pos += strlen(search_key);

  // Skip whitespace
  while (*key_pos && isspace(*key_pos))
    key_pos++;

  if (*key_pos == '"') {
    // String value
    value_start = key_pos + 1;
    value_end = value_start;

    // Find the end of the string (accounting for escaped quotes)
    int escaped = 0;
    while (*value_end) {
      if (escaped) {
        escaped = 0;
      } else if (*value_end == '\\') {
        escaped = 1;
      } else if (*value_end == '"') {
        break;
      }
      value_end++;
    }

    if (*value_end == '"') {
      value_len = value_end - value_start;
      value = (char *)malloc(value_len + 1);
      if (value) {
        strncpy(value, value_start, value_len);
        value[value_len] = '\0';

        // Unescape the string
        char *src = value;
        char *dst = value;
        escaped = 0;

        while (*src) {
          if (escaped) {
            *dst++ = *src++;
            escaped = 0;
          } else if (*src == '\\') {
            escaped = 1;
            src++;
          } else {
            *dst++ = *src++;
          }
        }
        *dst = '\0';
      }
    }
  }

  return value;
}

/**
 * Extract a numeric value from a JSON object
 */
static double extract_json_number(const char *json, const char *key) {
  char search_key[256];
  char *key_pos, *value_end;
  char value_str[64];

  // Format the key with quotes
  snprintf(search_key, sizeof(search_key), "\"%s\":", key);

  // Find the key in the JSON
  key_pos = strstr(json, search_key);
  if (!key_pos)
    return 0.0;

  // Move past the key and colon
  key_pos += strlen(search_key);

  // Skip whitespace
  while (*key_pos && isspace(*key_pos))
    key_pos++;

  // Find the end of the number
  value_end = key_pos;
  while (*value_end && (*value_end == '.' || *value_end == '-' ||
                        *value_end == '+' || isdigit(*value_end)))
    value_end++;

  // Copy the number into a string
  if (value_end > key_pos) {
    int value_len = value_end - key_pos;
    if (value_len >= sizeof(value_str))
      value_len = sizeof(value_str) - 1;

    strncpy(value_str, key_pos, value_len);
    value_str[value_len] = '\0';

    // Convert to double
    return atof(value_str);
  }

  return 0.0;
}

/**
 * Perform an HTTP GET request and return the response
 */
static char *http_get_request(const char *host, const char *path,
                              const char *query_params) {
  HINTERNET hInternet = NULL, hConnect = NULL, hRequest = NULL;
  char buffer[BUFFER_SIZE];
  DWORD bytesRead;
  char *response = NULL;
  DWORD responseSize = 0;
  DWORD totalSize = 0;
  BOOL success = FALSE;

  // Initialize WinINet
  hInternet = InternetOpen("LSH Weather Client/1.0",
                           INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
  if (!hInternet) {
    debug_print("InternetOpen failed with error: %lu\n", GetLastError());
    goto cleanup;
  }

  // Set timeouts to prevent hanging
  DWORD timeout = 15000; // 15 seconds
  InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout,
                    sizeof(timeout));
  InternetSetOption(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout,
                    sizeof(timeout));
  InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout,
                    sizeof(timeout));

  // Try HTTP instead of HTTPS
  hConnect = InternetConnect(hInternet, host, INTERNET_DEFAULT_HTTP_PORT, NULL,
                             NULL, INTERNET_SERVICE_HTTP, 0, 0);
  if (!hConnect) {
    debug_print("InternetConnect failed with error: %lu\n", GetLastError());
    goto cleanup;
  }

  // Create the request
  char full_path[1024];
  if (query_params) {
    snprintf(full_path, sizeof(full_path), "%s%s", path, query_params);
  } else {
    strcpy(full_path, path);
  }

  hRequest = HttpOpenRequest(hConnect, "GET", full_path, NULL, NULL, NULL,
                             INTERNET_FLAG_RELOAD, 0);
  if (!hRequest) {
    debug_print("HttpOpenRequest failed with error: %lu\n", GetLastError());
    goto cleanup;
  }

  // Add headers
  if (!HttpAddRequestHeaders(hRequest,
                             "User-Agent: LSH Weather Client\r\n"
                             "Accept: application/json\r\n",
                             -1, HTTP_ADDREQ_FLAG_ADD)) {
    debug_print("HttpAddRequestHeaders failed with error: %lu\n",
                GetLastError());
    goto cleanup;
  }

  // Handle security options for proxy servers
  DWORD securityFlags = 0;
  DWORD flagsSize = sizeof(securityFlags);
  InternetQueryOption(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &securityFlags,
                      &flagsSize);
  securityFlags |=
      SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_REVOCATION;
  InternetSetOption(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &securityFlags,
                    sizeof(securityFlags));

  // Send the request
  if (!HttpSendRequest(hRequest, NULL, 0, NULL, 0)) {
    debug_print("HttpSendRequest failed with error: %lu\n", GetLastError());
    goto cleanup;
  }

  // Check status code
  DWORD statusCode = 0;
  DWORD statusCodeSize = sizeof(statusCode);
  if (!HttpQueryInfo(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                     &statusCode, &statusCodeSize, NULL)) {
    debug_print("HttpQueryInfo failed with error: %lu\n", GetLastError());
    goto cleanup;
  }

  debug_print("HTTP status code: %lu\n", statusCode);
  if (statusCode != 200) {
    debug_print("Non-200 status code received: %lu\n", statusCode);
    goto cleanup;
  }

  // Allocate initial buffer for response
  responseSize = BUFFER_SIZE;
  response = (char *)malloc(responseSize);
  if (!response) {
    debug_print("Failed to allocate memory for response\n");
    goto cleanup;
  }

  // Read the response
  while (InternetReadFile(hRequest, buffer, BUFFER_SIZE - 1, &bytesRead) &&
         bytesRead > 0) {
    // Ensure we have enough space
    if (totalSize + bytesRead >= responseSize) {
      responseSize *= 2;
      char *new_response = (char *)realloc(response, responseSize);
      if (!new_response) {
        debug_print("Failed to reallocate memory for response\n");
        free(response);
        response = NULL;
        goto cleanup;
      }
      response = new_response;
    }

    // Append the new data
    memcpy(response + totalSize, buffer, bytesRead);
    totalSize += bytesRead;
  }

  // Add null terminator
  if (response) {
    if (totalSize >= responseSize) {
      responseSize++;
      response = (char *)realloc(response, responseSize);
      if (!response) {
        debug_print("Failed to reallocate memory for null terminator\n");
        goto cleanup;
      }
    }
    response[totalSize] = '\0';
    debug_print("Successfully read response: %d bytes\n", (int)totalSize);
  }

  success = TRUE;

cleanup:
  // Clean up in reverse order
  if (hRequest)
    InternetCloseHandle(hRequest);
  if (hConnect)
    InternetCloseHandle(hConnect);
  if (hInternet)
    InternetCloseHandle(hInternet);

  // If unsuccessful and response was allocated, free it
  if (!success && response) {
    free(response);
    response = NULL;
  }

  return response;
}
