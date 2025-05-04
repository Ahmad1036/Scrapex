#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <curl/curl.h>
#include "json.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using json = nlohmann::json;

// --- Configuration ---
const std::string YOUTUBE_API_KEY = "AIzaSyCR8VmhoJ0jfwX8XK_BTWWStR-5Fexew1Y"; // <<< --- REPLACE WITH YOUR KEY
const int YOUTUBE_SLAVE_PORT = 8083; // Choose a unique port

struct YouTubeVideo {
    std::string title;
    std::string videoId;
    std::string captions; // Store the captions.
};

// --- Helper Functions ---

// Callback function for libcurl (to capture HTTP response)
size_t WriteCallbackYoutube(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Function to URL encode a string
std::string urlEncode(const std::string& str) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;

    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        }
        else {
            encoded << '%' << std::setw(2) << int(c);
        }
    }

    return encoded.str();
}

// Function to retrieve YouTube video IDs based on query
std::vector<YouTubeVideo> getYoutubeVideos(const std::string& query, int maxResults = 3) {
    std::vector<YouTubeVideo> videos;

    if (YOUTUBE_API_KEY == "YOUR_YOUTUBE_API_KEY" || YOUTUBE_API_KEY.empty()) {
        std::cerr << "ERROR: YouTube API Key not set." << std::endl;
        return videos;  // Return empty vector
    }

    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        // Construct the YouTube Data API v3 search URL
        std::string encodedQuery = urlEncode(query);
        std::string apiUrl = "https://www.googleapis.com/youtube/v3/search"
            "?part=snippet"
            "&maxResults=" + std::to_string(maxResults) +
            "&q=" + encodedQuery +
            "&type=video"
            "&key=" + YOUTUBE_API_KEY;

        curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackYoutube);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "CURL failed: " << curl_easy_strerror(res) << std::endl;
        }
        else {
            std::cout << "YouTube API Raw Response: " << readBuffer << std::endl;
            try {
                json responseJson = json::parse(readBuffer);
                


                if (responseJson.contains("items") && responseJson["items"].is_array()) {
                    for (auto& item : responseJson["items"]) {
                        if (item.contains("id") && item["id"].contains("videoId") && item.contains("snippet") && item["snippet"].contains("title")) {
                            YouTubeVideo video;
                            video.videoId = item["id"]["videoId"].get<std::string>();
                            video.title = item["snippet"]["title"].get<std::string>();
                            videos.push_back(video);
                        }
                    }
                }
            }
            catch (json::parse_error& e) {
                std::cerr << "JSON parsing failed: " << e.what() << std::endl;
            }
        }
        curl_easy_cleanup(curl);
    }

    return videos;
}

// Function to retrieve captions for a specific YouTube video
std::string getYoutubeVideoCaptions(const std::string& videoId) {
    std::string captions = "";

    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        // Construct the YouTube Data API v3 captions list URL
        std::string apiUrl = "https://www.googleapis.com/youtube/v3/captions"
            "?part=snippet"
            "&videoId=" + videoId +
            "&key=" + YOUTUBE_API_KEY;

        curl_easy_setopt(curl, CURLOPT_URL, apiUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackYoutube);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "CURL failed: " << curl_easy_strerror(res) << std::endl;
        }
        else {
            try {
                json responseJson = json::parse(readBuffer);

                if (responseJson.contains("items") && responseJson["items"].is_array()) {
                    if (!responseJson["items"].empty()) {
                        std::string captionId = responseJson["items"][0]["id"].get<std::string>();

                        // Construct the YouTube Data API v3 caption download URL
                        std::string captionUrl = "https://www.googleapis.com/youtube/v3/captions/" + captionId
                            + "?key=" + YOUTUBE_API_KEY
                            + "&tfmt=srt"; // Requesting SRT format for simplicity

                        readBuffer.clear();
                        curl_easy_setopt(curl, CURLOPT_URL, captionUrl.c_str());
                        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackYoutube);
                        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

                        res = curl_easy_perform(curl);

                        if (res != CURLE_OK) {
                            std::cerr << "CURL failed to download captions: " << curl_easy_strerror(res) << std::endl;
                        }
                        else {
                            captions = readBuffer;
                        }
                    }
                }
            }
            catch (json::parse_error& e) {
                std::cerr << "JSON parsing failed: " << e.what() << std::endl;
            }
        }

        curl_easy_cleanup(curl);
    }

    return captions;
}

// --- Socket Helper Functions (Send/Receive Size) ---

void send_data_to_master(SOCKET sock, const std::string& data) {
    uint32_t dataSize = data.size();
    uint32_t netSize = htonl(dataSize);

    // Send the size
    int sizeSent = send(sock, reinterpret_cast<const char*>(&netSize), sizeof(netSize), 0);
    if (sizeSent != sizeof(netSize)) {
        std::cerr << "YouTube Slave: Failed to send data size. Error: " << WSAGetLastError() << std::endl;
        return;
    }
    std::cout << "YouTube Slave: Sent data size: " << dataSize << " bytes." << std::endl;

    // Send the data
    if (dataSize > 0) {
        int totalSent = 0;
        int bytesLeft = dataSize;
        const char* ptr = data.c_str();

        while (bytesLeft > 0) {
            int bytesSent = send(sock, ptr + totalSent, bytesLeft, 0);
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "YouTube Slave: Send (data) failed with error: " << WSAGetLastError() << std::endl;
                return;
            }
            if (bytesSent == 0) {
                std::cerr << "YouTube Slave: Send (data) returned 0, connection possibly closed." << std::endl;
                return;
            }
            totalSent += bytesSent;
            bytesLeft -= bytesSent;
        }
        std::cout << "YouTube Slave: Sent video data (" << totalSent << " bytes)." << std::endl;
    }
    else {
        std::cout << "YouTube Slave: Skipping send for 0-byte content." << std::endl;
    }
}

// --- Main function ---
int main() {
    // --- Initialize Winsock ---
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) {
        std::cerr << "YouTube Slave: WSAStartup failed: " << wsaResult << std::endl;
        return 1;
    }

    // --- Setup Server Socket ---
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "YouTube Slave: Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(YOUTUBE_SLAVE_PORT);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "YouTube Slave: Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "YouTube Slave: Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "YouTube Slave ready.  Listening on port " << YOUTUBE_SLAVE_PORT << "..." << std::endl;

    while (true) { // Main Accept Loop
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "YouTube Slave: Accept failed: " << WSAGetLastError() << std::endl;
            continue;
        }

        std::cout << "\nYouTube Slave: Master connected." << std::endl;

        // --- Receive Data (Search Query) ---
        uint32_t netSize = 0;
        int sizeBytesReceived = recv(clientSocket, reinterpret_cast<char*>(&netSize), sizeof(netSize), MSG_WAITALL);

        if (sizeBytesReceived == SOCKET_ERROR || sizeBytesReceived == 0 || sizeBytesReceived != sizeof(netSize)) {
            if (sizeBytesReceived == 0) std::cout << "YouTube Slave: Master disconnected before sending query size." << std::endl;
            else std::cerr << "YouTube Slave: Recv failed or incomplete (size): " << WSAGetLastError() << std::endl;
            closesocket(clientSocket);
            continue;
        }

        uint32_t querySize = ntohl(netSize);
        std::cout << "YouTube Slave: Expecting " << querySize << " bytes of query data." << std::endl;

        std::string searchQuery;
        if (querySize > 0) {
            searchQuery.resize(querySize);
            int totalBytesReceived = 0;
            while (totalBytesReceived < querySize) {
                int bytesReceived = recv(clientSocket, &searchQuery[totalBytesReceived], querySize - totalBytesReceived, 0);
                if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
                    if (bytesReceived == 0) std::cerr << "YouTube Slave: Master disconnected during query data receive." << std::endl;
                    else std::cerr << "YouTube Slave: Recv failed (query data): " << WSAGetLastError() << std::endl;
                    closesocket(clientSocket);
                    continue;
                }
                totalBytesReceived += bytesReceived;
            }
            std::cout << "YouTube Slave: Successfully received query: " << searchQuery << std::endl;
        }
        else {
            std::cout << "YouTube Slave: Received size 0. No query data." << std::endl;
            closesocket(clientSocket);
            continue;
        }

        // --- Get YouTube Videos and Captions ---
        std::vector<YouTubeVideo> videos = getYoutubeVideos(searchQuery, 3);
        json responseJson;
        responseJson["videos"] = json::array();

        for (auto& video : videos) {
            video.captions = getYoutubeVideoCaptions(video.videoId);
            json videoJson;
            videoJson["title"] = video.title;
            videoJson["videoId"] = video.videoId;
            videoJson["captions"] = video.captions;
            responseJson["videos"].push_back(videoJson);
        }

        std::string responseString = responseJson.dump();

        // --- Send Response to Master ---
        send_data_to_master(clientSocket, responseString);

        // --- Cleanup ---
        std::cout << "YouTube Slave: Closing client socket." << std::endl;
        closesocket(clientSocket);
        std::cout << "YouTube Slave: Ready for next connection." << std::endl;
    }

    // --- Global Cleanup ---
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}