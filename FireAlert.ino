#include <WiFi.h>
#include <ESP8266WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "F22";
const char* password = "mdab23092004";

// Pin Definitions
#define MQ2_PIN 34  // Use GPIO 34 (ADC1_6) for analog input on ESP32

// Constants for sensor calibration
const int CALIBRATION_SAMPLES = 100;
const int WINDOW_SIZE = 10;
int smokeReadings[WINDOW_SIZE];
int readIndex = 0;
long smokeTotal = 0;

// Thresholds for smoke detection
const int SMOKE_THRESHOLD_HIGH = 400;
const int SMOKE_THRESHOLD_LOW = 350;
bool smokeAlarmActive = false;

// Create web server instance
WebServer server(80);

// Variables to store sensor reading
int smokeLevel = 0;
bool isSmokeDetected = false;
String userLocation = "";
float latitude = 0;
float longitude = 0;

// Function to get filtered smoke reading
int getFilteredSmokeReading() {
  smokeTotal = smokeTotal - smokeReadings[readIndex];
  smokeReadings[readIndex] = analogRead(MQ2_PIN);
  smokeTotal = smokeTotal + smokeReadings[readIndex];
  readIndex = (readIndex + 1) % WINDOW_SIZE;
  return smokeTotal / WINDOW_SIZE;
}

// Function to check smoke with hysteresis
bool checkSmokeAlarm(int smokeValue) {
  if(smokeValue > SMOKE_THRESHOLD_HIGH) {
    smokeAlarmActive = true;
  } else if(smokeValue < SMOKE_THRESHOLD_LOW) {
    smokeAlarmActive = false;
  }
  return smokeAlarmActive;
}

void setup() {
  Serial.begin(115200);
  
  // Initialize smoke readings array
  for (int i = 0; i < WINDOW_SIZE; i++) {
    smokeReadings[i] = 0;
  }

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Get approximate location based on IP
    getLocationFromIP();
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }

  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/readings", handleReadings);
  
  // Start server
  server.begin();
}

void getLocationFromIP() {
  HTTPClient http;
  http.begin("http://ip-api.com/json"); // Free IP geolocation API
  
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // Parse JSON response
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      latitude = doc["lat"];
      longitude = doc["lon"];
      userLocation = doc["city"].as<String>() + ", " + doc["regionName"].as<String>();
    }
  }
  
  http.end();
}

void loop() {
  // Handle web client requests
  server.handleClient();
  
  // Get filtered smoke reading
  smokeLevel = getFilteredSmokeReading();
  
  // Check if smoke is detected with hysteresis
  isSmokeDetected = checkSmokeAlarm(smokeLevel);
  
  // Print to Serial Monitor for debugging
  Serial.print("Smoke Level: ");
  Serial.println(smokeLevel);
  
  delay(1000);  // Wait for a second before next reading
}

void handleRoot() {
  String html = R"=====(
<!DOCTYPE html>
<html>
<head>
    <title>Smart Smoke Detector</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: Arial; 
            margin: 20px; 
            text-align: center;
            background-color: #f0f0f0;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
            padding: 20px;
            background-color: white;
            border-radius: 10px;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        .readings {
            font-size: 24px;
            margin: 20px 0;
            padding: 20px;
            border-radius: 5px;
        }
        .alert {
            background-color: #ff4444;
            color: white;
            padding: 20px;
            border-radius: 5px;
            margin: 20px 0;
            display: none;
        }
        .safe {
            background-color: #44ff44;
            color: white;
            padding: 20px;
            border-radius: 5px;
            margin: 20px 0;
            display: none;
        }
        .emergency-info {
            background-color: #fff3cd;
            padding: 20px;
            border-radius: 5px;
            margin: 20px 0;
            text-align: left;
            display: none;
        }
        .safety-measures {
            background-color: #cce5ff;
            padding: 20px;
            border-radius: 5px;
            margin: 20px 0;
            text-align: left;
        }
        .map {
            height: 300px;
            margin: 20px 0;
            border-radius: 5px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Smart Smoke Detection System</h1>
        <div class="readings" id="smokeReading">Loading...</div>
        <div class="safe" id="safeMessage">Environment is Safe</div>
        
        <div class="alert" id="alertMessage">
            <h2>⚠ SMOKE DETECTED! ⚠</h2>
            <p>Please take immediate action!</p>
        </div>
        
        <div class="emergency-info" id="emergencyInfo">
            <h3>📍 Your Location: <span id="location">Loading...</span></h3>
            <h3>🚒 Nearby Emergency Services:</h3>
            <div id="emergencyServices">Loading nearby services...</div>
        </div>

        <div class="safety-measures">
            <h3>🚨 Safety Measures:</h3>
            <ol>
                <li>Stay calm and alert others in the vicinity</li>
                <li>If you see fire, evacuate immediately</li>
                <li>Call emergency services (911 or local emergency number)</li>
                <li>Use stairs, not elevators</li>
                <li>Stay low to avoid smoke inhalation</li>
                <li>Meet at designated assembly point</li>
                <li>Do not re-enter until declared safe</li>
            </ol>
        </div>
        
        <div id="map" class="map"></div>
    </div>
    
    <script>
        let map;
        let service;
        let infowindow;

        function initMap() {
            // Initialize map with user location
            fetch('/readings')
                .then(response => response.json())
                .then(data => {
                    const position = { lat: data.lat, lng: data.lng };
                    map = new google.maps.Map(document.getElementById('map'), {
                        center: position,
                        zoom: 13
                    });

                    // Search for nearby emergency services
                    searchNearbyServices(position);
                });
        }

        function searchNearbyServices(position) {
            const types = ['fire_station', 'hospital'];
            const emergencyServicesDiv = document.getElementById('emergencyServices');
            let servicesHTML = '';

            types.forEach(type => {
                const request = {
                    location: position,
                    radius: '5000',
                    type: type
                };

                service = new google.maps.places.PlacesService(map);
                service.nearbySearch(request, (results, status) => {
                    if (status === google.maps.places.PlacesServiceStatus.OK) {
                        results.slice(0, 3).forEach(place => {
                            const icon = type === 'fire_station' ? '🚒' : '🏥';
                            servicesHTML += <p>${icon} ${place.name} - ${place.vicinity}</p>;
                            
                            // Add marker to map
                            new google.maps.Marker({
                                map: map,
                                position: place.geometry.location,
                                title: place.name
                            });
                        });
                        emergencyServicesDiv.innerHTML = servicesHTML;
                    }
                });
            });
        }

        function updateReadings() {
            fetch('/readings')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('smokeReading').innerHTML = 
                        'Smoke Level: ' + data.smoke;
                    document.getElementById('location').innerHTML = data.location;
                    
                    // Show/hide alert based on smoke detection
                    document.getElementById('alertMessage').style.display = 
                        data.isSmoke ? 'block' : 'none';
                    document.getElementById('safeMessage').style.display = 
                        data.isSmoke ? 'none' : 'block';
                    document.getElementById('emergencyInfo').style.display = 
                        data.isSmoke ? 'block' : 'none';
                });
        }
        
        // Update readings every second
        setInterval(updateReadings, 1000);
        updateReadings();
    </script>
    <script async defer
        src="https://maps.googleapis.com/maps/api/js?key=YOUR_GOOGLE_MAPS_API_KEY&libraries=places&callback=initMap">
    </script>
</body>
</html>
)=====";
  server.send(200, "text/html", html);
}

void handleReadings() {
  String json = "{";
  json += "\"smoke\":" + String(smokeLevel) + ",";
  json += "\"isSmoke\":" + String(isSmokeDetected ? "true" : "false") + ",";
  json += "\"location\":\"" + userLocation + "\",";
  json += "\"lat\":" + String(latitude, 6) + ",";
  json += "\"lng\":" + String(longitude, 6);
  json += "}";
  server.send(200, "application/json", json);
}
