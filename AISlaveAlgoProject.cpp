#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <sstream>



// Include the nlohmann/json header
#include "json.hpp" // Adjust path if needed

#pragma comment(lib, "ws2_32.lib")
// #pragma comment(lib, "libcurl.lib") // If using static libcurl

// Use the nlohmann::json namespace
using json = nlohmann::json;

// --- Configuration ---
const std::string GEMINI_API_KEY = "AIzaSyDr_NjF-eOh2xnvrYD75ftb-tB8778BGto"; // <<< --- REPLACE WITH YOUR KEY
const int AI_SLAVE_PORT = 8081;

// Callback function for libcurl (to capture HTTP response)
size_t WriteCallbackAi(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Function to call the Gemini API
std::string callGeminiApi(const std::string& htmlContent) {
    if (GEMINI_API_KEY == "YOUR_GEMINI_API_KEY" || GEMINI_API_KEY.empty()) {
        std::cerr << "ERROR: Gemini API Key not set." << std::endl;
        return "Error: API Key not configured.";
    }
    if (htmlContent.empty()) {
        std::cerr << "Warning: Skipping Gemini call for empty HTML content." << std::endl;
        return "NO_RECIPE_FOUND";
    }

    CURL* curl;
    CURLcode res;
    std::string readBuffer;
    std::string extractedText = "NO_RECIPE_FOUND"; // Default response

    curl = curl_easy_init();
    if (curl) {
        std::string apiUrl = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=" + GEMINI_API_KEY; // NEW


        // --- Construct the Prompt ---
        // This prompt is crucial for getting good results. Adjust as needed.
        std::string prompt =
            "You are a recipe extraction assistant. Given the following HTML content of a webpage, "
            "extract ONLY the ingredients list and the step-by-step cooking instructions. "
            "Present the ingredients first, followed by the instructions. "
            "Format the output clearly and concisely. "
            "Ignore all other text, including titles (unless part of the recipe itself), introductory paragraphs, "
            "closing remarks, comments, author information, advertisements, headers, footers, navigation links, "
            "and any non-recipe related content found in the HTML. "
            "If the HTML does not appear to contain a discernible recipe (ingredients and steps), "
            "respond ONLY with the exact phrase 'NO_RECIPE_FOUND'. Do not add any explanation."
            "\n\n--- START OF HTML CONTENT ---\n" +
            htmlContent +
            "\n--- END OF HTML CONTENT ---";

        // --- Construct the JSON Payload ---
        json requestPayload;
        requestPayload["contents"][0]["parts"][0]["text"] = prompt;
        // Optional: Add generationConfig or safetySettings if needed
        // requestPayload["generationConfig"]["temperature"] = 0.5;
        // requestPayload["safetySettings"]...

        std::string jsonString = requestPayload.dump(); // Serialize JSON to string

        // --- Set CURL Options ---
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonString.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, jsonString.length());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackAi);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L); // Increase timeout for potentially long AI responses
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Enable for debugging curl

        // --- Perform the Request ---
        std::cout << "Calling Gemini API..." << std::endl;
        res = curl_easy_perform(curl);
        std::cout << "Gemini API call finished." << std::endl;

        // --- Check for CURL Errors ---
        if (res != CURLE_OK) {
            std::cerr << "CURL failed: " << curl_easy_strerror(res) << std::endl;
            extractedText = "Error: CURL failed.";
        }
        else {
            // --- Check HTTP Status Code ---
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            std::cout << "Gemini API HTTP Status Code: " << http_code << std::endl;

            if (http_code == 200) {
                // --- Parse the JSON Response ---
                try {
                    json responseJson = json::parse(readBuffer);
                    // Navigate the JSON structure to get the text
                    // Structure: response -> candidates -> [0] -> content -> parts -> [0] -> text
                    if (responseJson.contains("candidates") && responseJson["candidates"].is_array() && !responseJson["candidates"].empty() &&
                        responseJson["candidates"][0].contains("content") && responseJson["candidates"][0]["content"].contains("parts") &&
                        responseJson["candidates"][0]["content"]["parts"].is_array() && !responseJson["candidates"][0]["content"]["parts"].empty() &&
                        responseJson["candidates"][0]["content"]["parts"][0].contains("text"))
                    {
                        extractedText = responseJson["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
                        std::cout << "Successfully extracted text from Gemini response." << std::endl;
                    }
                    else {
                        std::cerr << "Error: Unexpected JSON structure in Gemini response." << std::endl;
                        std::cerr << "Response: " << readBuffer << std::endl;
                        extractedText = "Error: Unexpected Gemini JSON structure.";
                    }
                }
                catch (json::parse_error& e) {
                    std::cerr << "JSON parsing failed: " << e.what() << std::endl;
                    std::cerr << "Response: " << readBuffer << std::endl;
                    extractedText = "Error: Failed to parse Gemini JSON response.";
                }
            }
            else {
                std::cerr << "Gemini API returned HTTP error: " << http_code << std::endl;
                std::cerr << "Response body: " << readBuffer << std::endl; // Log the error response
                extractedText = "Error: Gemini API returned HTTP " + std::to_string(http_code);
            }
        }

        // Cleanup
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    else {
        std::cerr << "Error initializing CURL for Gemini." << std::endl;
        extractedText = "Error: Failed to initialize CURL.";
    }

    return extractedText;
}

// --- Function to send data with size prefix (similar to ScraperSlave) ---
void send_data_to_master(SOCKET sock, const std::string& data) {
    uint32_t dataSize = data.size();
    uint32_t netSize = htonl(dataSize); // Convert size to network byte order

    // 1. Send the size (4 bytes)
    int sizeSent = send(sock, reinterpret_cast<const char*>(&netSize), sizeof(netSize), 0);
    if (sizeSent != sizeof(netSize)) {
        std::cerr << "AI Slave: Failed to send data size. Error: " << WSAGetLastError() << std::endl;
        return;
    }
    std::cout << "AI Slave: Sent data size: " << dataSize << " bytes." << std::endl;


    // 2. Send the actual data (only if size > 0)
    if (dataSize > 0) {
        int totalSent = 0;
        int bytesLeft = dataSize;
        const char* ptr = data.c_str();

        while (bytesLeft > 0) {
            int bytesSent = send(sock, ptr + totalSent, bytesLeft, 0);
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "AI Slave: Send (data) failed with error: " << WSAGetLastError() << std::endl;
                return;
            }
            if (bytesSent == 0) {
                std::cerr << "AI Slave: Send (data) returned 0, connection possibly closed." << std::endl;
                return;
            }
            totalSent += bytesSent;
            bytesLeft -= bytesSent;
        }
        std::cout << "AI Slave: Sent recipe data (" << totalSent << " bytes)." << std::endl;
    }
    else {
        std::cout << "AI Slave: Skipping send for 0-byte content." << std::endl;
    }
}

// --- aiSlaveProject.cpp ---
// ... (includes, functions like callGeminiApi, send_data_to_master remain the same) ...

int main() {
    // --- Initialize Winsock (outside the loop) ---
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) { /* ... error handling ... */ return 1; }

    // --- Setup Server Socket (outside the loop) ---
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) { /* ... error handling ... */ WSACleanup(); return 1; }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(AI_SLAVE_PORT); // AI Port

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) { /* ... bind error ... */ closesocket(serverSocket); WSACleanup(); return 1; }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) { /* ... listen error ... */ closesocket(serverSocket); WSACleanup(); return 1; }

    std::cout << "AI Slave ready. Waiting for master connections on port " << AI_SLAVE_PORT << "..." << std::endl;

    // --- Main Accept Loop ---
    while (true) // <<< --- ADDED LOOP --- <<<
    {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        // Note: Don't close serverSocket here anymore if looping

        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "AI Slave: Accept failed: " << WSAGetLastError() << std::endl;
            // Maybe add a delay or break condition if accept fails repeatedly
            continue; // Try accepting again
        }
        std::cout << "\nAI Slave: Master connected. Processing request..." << std::endl;

        // --- Receive Data from Master (Size Prefix Protocol) ---
        // This part handles ONE request sequence from the connected master
        bool connectionActive = true;
        // Master sends HTML -> AI processes -> AI sends Recipe -> Master disconnects
        // So, this inner loop effectively runs once per connection in the current design.

        // 1. Receive the size of the upcoming HTML
        std::cout << "AI Slave: Waiting for HTML size..." << std::endl;
        uint32_t netSize = 0;
        int sizeBytesReceived = recv(clientSocket, reinterpret_cast<char*>(&netSize), sizeof(netSize), MSG_WAITALL);

        if (sizeBytesReceived == SOCKET_ERROR || sizeBytesReceived == 0 || sizeBytesReceived != sizeof(netSize)) {
            if (sizeBytesReceived == 0) std::cout << "AI Slave: Master disconnected before sending HTML size." << std::endl;
            else std::cerr << "AI Slave: Recv failed or incomplete (size): " << WSAGetLastError() << std::endl;
            closesocket(clientSocket); // Close this client connection
            continue; // Go back to accept the next connection
        }

        uint32_t htmlDataSize = ntohl(netSize);
        std::cout << "AI Slave: Expecting " << htmlDataSize << " bytes of HTML data." << std::endl;

        // 2. Receive the actual HTML data
        std::string receivedHtml;
        if (htmlDataSize > 0) {
            receivedHtml.resize(htmlDataSize);
            int totalBytesReceived = 0;
            while (totalBytesReceived < htmlDataSize) {
                int bytesReceived = recv(clientSocket, &receivedHtml[totalBytesReceived], htmlDataSize - totalBytesReceived, 0);
                if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
                    if (bytesReceived == 0) std::cerr << "AI Slave: Master disconnected during HTML data receive." << std::endl;
                    else std::cerr << "AI Slave: Recv failed (HTML data): " << WSAGetLastError() << std::endl;
                    connectionActive = false; break;
                }
                totalBytesReceived += bytesReceived;
            }
            if (!connectionActive || totalBytesReceived != htmlDataSize) {
                std::cerr << "AI Slave: Error receiving HTML data fully." << std::endl;
                closesocket(clientSocket); // Close this client connection
                continue; // Go back to accept the next connection
            }
            std::cout << "AI Slave: Successfully received " << receivedHtml.size() << " bytes of HTML." << std::endl;
        }
        else {
            std::cout << "AI Slave: Received size 0. No HTML data to process." << std::endl;
            receivedHtml.clear(); // Ensure empty
        }

        // --- Process with Gemini ---
        std::string recipeText = "NO_RECIPE_FOUND"; // Default if HTML was empty
        if (!receivedHtml.empty()) {
            recipeText = callGeminiApi(receivedHtml);
            std::cout << "AI Slave: Gemini Result Size: " << recipeText.size() << " bytes." << std::endl;
        }
        else {
            std::cout << "AI Slave: Skipping Gemini call due to empty HTML." << std::endl;
        }

        // --- Send Result Back to Master ---
        send_data_to_master(clientSocket, recipeText);

        std::cout << "AI Slave: Finished processing request." << std::endl;

        // --- Cleanup for this client ---
        std::cout << "AI Slave: Closing client socket." << std::endl;
        closesocket(clientSocket);
        std::cout << "AI Slave: Ready for next connection." << std::endl;

    } // --- End of while(true) Accept Loop ---


    // --- Global Cleanup (Might not be reached) ---
    std::cout << "AI Slave shutting down." << std::endl;
    closesocket(serverSocket); // Close the listening socket
    WSACleanup();
    return 0;
}