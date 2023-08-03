/*
  NostrRelayManager - Arduino library for Nostr.
  Created by Black Coffee <bc@omg.lol>, March 29th 2023
  Released under MIT License
*/
#include <NostrRelayManager.h>
#include <NostrEvent.h>
#include <WebSocketsClient.h>
#include "ArduinoJson.h"
#include <vector>

/**
 * @brief Construct a new Nostr Relay Manager:: Nostr Relay Manager object
 * 
 */
NostrRelayManager::NostrRelayManager() : 
    lastBroadcastAttempt(0),
    minRelaysTimeout(15000),
    minRelays(1),
    relays(std::vector<String>()) {
}

/**
 * @brief Specify a callback for a relay event type
 * 
 * @param key Can be "ok", "nip01", "nip04", "zapReceipt"
 * @param callback 
 */
void NostrRelayManager::setEventCallback(const std::string& key, EventCallbackFn callback) {
    m_callbacks[key] = callback;
}

void NostrRelayManager::setEventCallback(const int key, EventCallbackFn callback) {
    m_callbacks[std::to_string(key)] = callback;
}

/**
 * @brief Run a specified event callback
 * 
 * @param key Can be "ok", "nip01", "nip04", "zapReceipt"
 * @param payload 
 */
void NostrRelayManager::performEventAction(const std::string& key, const char* payload) {
    auto it = m_callbacks.find(key);
    if (it != m_callbacks.end()) {
        it->second(key, payload); // pass in the argument to the callback function
    }
}

/**
 * @brief Add a relay message to the queue
 * 
 * @param item 
 */
void NostrRelayManager::enqueueMessage(const char item[NostrQueueProcessor::MAX_ITEM_SIZE]) {
    m_queue.enqueue(item);
}

/**
 * @brief Are there messages in the message queue?
 * 
 * @return true 
 * @return false 
 */
bool NostrRelayManager::hasEnqueuedMessages() {
    return !m_queue.isEmpty();
}

/**
 * @brief Queue an event request message to be sent to relays
 * 
 * @param options A NostrRequestOptions object with arguments for the requested event(s)
 */
void NostrRelayManager::requestEvents(const NostrRequestOptions* options) {
    // now put the json within a REQ to be sent to the relays
    String serialisedJson = "[\"REQ\", \"" + getNewSubscriptionId() + "\"," + options->toJson() + "]";
    Serial.println(F("REQ looks like this"));
    Serial.println(serialisedJson);

    enqueueMessage(serialisedJson.c_str());
}

/**
 * @brief Set the relays to use
 * 
 * @param new_relays 
 * @param size 
 */
void NostrRelayManager::setRelays(const std::vector<String>& new_relays) {
    relays.assign(new_relays.begin(), new_relays.end());
}

/**
* @brief Keep all relays active
* 
*/
void NostrRelayManager::loop() {
    for (int i = 0; i < relays.size(); i++) {
        _webSocketClients[i].loop();
    }
}

/**
 * @brief broadcast message to one relay only, identified by it's URL
 * 
 * @param serializedEventJson 
 * @param relayUrl 
 */
void NostrRelayManager::broadcastEventToRelay(String serializedEventJson, String relayUrl) {
    Serial.println("Broadcasting event  " + serializedEventJson);
    for (int i = 0; i < relays.size(); i++) {
        _webSocketClients[i].sendTXT(serializedEventJson);
        // delay(1000);
    }
}

/**
 * @brief Broadcasts messages in m_queue
 * 
 */
void NostrRelayManager::broadcastEvents() {

  unsigned long currentMillis = millis();
  if (m_queue.isEmpty()) {
    return;
  }

  if (connectedRelayCount() >= minRelays && currentMillis - lastBroadcastAttempt <= minRelaysTimeout) {
    // Broadcast all queued events to connected relays
    while (!m_queue.isEmpty()) {
      const char *item = m_queue.dequeue();
      Serial.printf("Broadcasting event: %s\n", item);
        broadcastEvent(String(item));
    }

    lastBroadcastAttempt = currentMillis;
  } else if (currentMillis - lastBroadcastAttempt > minRelaysTimeout && connectedRelayCount() > 0) {
    // Broadcast all queued events to all relays, regardless of whether they're connected
    while (!m_queue.isEmpty()) {
      const char *item = m_queue.dequeue();
      Serial.printf("Broadcasting event: %s\n", item);
      broadcastEvent(String(item));
    }

    lastBroadcastAttempt = currentMillis;
  }
}

/**
 * @brief How many relays are currently connected?
 * 
 * @return int 
 */
int NostrRelayManager::connectedRelayCount() {
    int count = 0;
    for (int i = 0; i < relays.size(); i++) {
        if (_webSocketClients[i].isConnected()) {
            count++;
        }
    }
    return count;
}

/**
 * @brief set minimum number of relays and a timeout for this minimum threshold.
 * After this timeout is reached, the message will be broadcast regardless of the 
 * number of connected relays
 * 
 * @param minRelays 
 * @param minRelaysTimeout in milliseconds
 */
void NostrRelayManager::setMinRelaysAndTimeout(int minRelays, unsigned long minRelaysTimeout) {
    this->minRelays = minRelays;
    this->minRelaysTimeout = minRelaysTimeout;
}

/**
 * @brief Print the relay at index: index
 * 
 * @param index 
 */
void NostrRelayManager::printRelay(int index) const {
    if (index >= 0 && index < relays.size()) {
        Serial.println("Relay is " + String(relays[index]));
    } else {
        Serial.println("Invalid index.");
    }
}

/**
 * @brief Connect to all specified relays
 * 
 * @param callback 
 */
void NostrRelayManager::connect(std::function<void(WStype_t, uint8_t*, size_t)> callback) {
    // Initialize WebSocket connections
    // serial print all relays
    Serial.println("Relays are:");
    for (int i = 0; i < relays.size(); i++) {
        Serial.println(relays[i]);
    }
    for (int i = 0; i < relays.size(); i++) {
        // if the relay includes a "/" split on the first / and use the first part as the host and the second part as the path
        if(String(relays[i]).indexOf("/") > 0) {
            String host = String(relays[i]).substring(0, String(relays[i]).indexOf("/"));
            String path = String(relays[i]).substring(String(relays[i]).indexOf("/"));
            Serial.println("Connecting to relay: " + host + " with path: " + path);
            _webSocketClients[i].beginSSL(host, 443, path);
        } else {
            Serial.println("Connecting to relay: " + String(relays[i]));
            _webSocketClients[i].beginSSL(relays[i], 443);
        }
        _webSocketClients[i].setReconnectInterval(5000);
        _webSocketClients[i].onEvent([this, callback, i](WStype_t type, uint8_t* payload, size_t length) {
            this->_webSocketEvent(type, payload, length, i);

            if (callback != nullptr) {
                callback(type, payload, length);
            }
        });
    }
    delay(500);
}

/**
 * @brief Disconnect from all relays
 * 
 */
void NostrRelayManager::disconnect() {
    for (int i = 0; i < relays.size(); i++) {
        _webSocketClients[i].disconnect();
        delay(1000);
    }
}

/**
 * @brief Broadcast a serialisedEvent JSON to all relays
 * 
 * @param serializedEventJson 
 */
void NostrRelayManager::broadcastEvent(String serializedEventJson) {
    for (int i = 0; i < relays.size(); i++) {
        _webSocketClients[i].sendTXT(serializedEventJson);
        delay(1000);
    }
}

/**
 * @brief Generate a random alphanumeric string to be used for subscription IDs
 * 
 * @return String 
 */
String NostrRelayManager::getNewSubscriptionId() {
    String chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    String result = "";
    int length = 64;
    for (int i = 0; i < length; i++) {
        int randomIndex = random(chars.length());
        result += chars[randomIndex];
    }
return result;
}



/**
 * @brief Generic websocket event callback
 * 
 * @param type 
 * @param payload 
 * @param length 
 * @param relayIndex 
 */
void NostrRelayManager::_webSocketEvent(WStype_t type, uint8_t* payload, size_t length, int relayIndex) {
  Serial.println("Message from relay index:" + String(relayIndex));
  switch (type) {
    case WStype_DISCONNECTED:
        Serial.printf("[WSc] Disconnected from relay: %s\n", ".");
        performEventAction("disconnected", relays[relayIndex].c_str());
        break;
    case WStype_CONNECTED:
        Serial.printf("[WSc] Connected to relay: %s\n", ".");
        performEventAction("connected", "message");
    break;
    case WStype_TEXT:
    {
        Serial.printf("[WSc]: Received text from relay %s: %s\n", ".", payload);
        const char *payloadStr = (char *)payload;

        // if the string is a JSON string, convert it to a JSON object
        if (String(payloadStr).indexOf("[") == 0) {
            StaticJsonDocument<4096> doc;
            DeserializationError error = deserializeJson(doc, payloadStr);
            if (error) {
                Serial.print(F("deserializeJson() failed: "));
                Serial.println(error.c_str());
                return;
            }
            // get kind int from kind = doc[2]["kind"]; and handle exceptions
            
            

            int kind = 0;

            try {
                kind = doc[2]["kind"];
                try {
                    performEventAction(std::to_string(kind), payloadStr);
                } catch (const std::out_of_range& oor) {
                    Serial.println("No callback for event kind: " + String(kind));
                }
            }
            catch (const std::out_of_range& oor) {
                Serial.println("No kind in JSON string, not doing anything.");
                return;
            }
                
                
            doc.clear();
        } else if (String(payloadStr).indexOf("\"OK\"") > 0) {
            performEventAction("ok", payloadStr);
        } else {
            Serial.println("Received text from relay: " + String(payloadStr));
            Serial.println("Not a JSON string, not an OK string, not doing anything.");
        }
        break;
    }
    case WStype_ERROR:
        Serial.printf("[WSc] Error from relay %s: %s\n", ".", payload);
        break;
    default:
        break;
  }
}