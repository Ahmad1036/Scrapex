#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <fstream>
#include <sstream>

// --- libxml2 Includes ---
#include <libxml/HTMLparser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/HTMLtree.h> // Needed for htmlDocDumpMemory
#include <libxml/xpath.h> // Add this include
#include <libxml/xpathInternals.h> // Add this include

#pragma comment(lib, "ws2_32.lib")
// #pragma comment(lib, "libcurl.lib") // If using static libcurl
// #pragma comment(lib, "libxml2.lib") // If using static libxml2

// Callback function for curl (unchanged)
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Function to fetch content using curl (unchanged)
std::string get_request(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string result;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Optional: enable for debugging curl
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");

        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "CURL Error: " << curl_easy_strerror(res) << " for URL: " << url << std::endl;
            result.clear(); // Ensure empty result on error
        }
        else {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code != 200) {
                std::cerr << "HTTP request failed with code: " << http_code << " for URL: " << url << std::endl;
                // Optionally clear result if you only want 200 responses
                result.clear();
            }
        }
        curl_easy_cleanup(curl);
    }
    return result;
}

// --- HTML Cleaning Function using libxml2 (unchanged) ---
void removeNodesByName(xmlNode* node, const char* nodeName) {
    xmlNode* cur_node = NULL;
    std::vector<xmlNode*> nodes_to_remove;

    for (cur_node = node->children; cur_node; ) {
        xmlNode* next_node = cur_node->next; // Store next sibling *before* potential removal

        if (cur_node->type == XML_ELEMENT_NODE &&
            xmlStrcasecmp(cur_node->name, (const xmlChar*)nodeName) == 0) {
            nodes_to_remove.push_back(cur_node);
        }
        else {
            // Recurse into children only if it's an element node
            if (cur_node->type == XML_ELEMENT_NODE) {
                removeNodesByName(cur_node, nodeName);
            }
        }
        cur_node = next_node; // Move to the next sibling
    }

    for (xmlNode* node_to_remove : nodes_to_remove) {
        xmlUnlinkNode(node_to_remove);
        xmlFreeNode(node_to_remove);
    }
}

std::string extractPotentialRecipeHtml(htmlDocPtr doc) {
    if (!doc) return "";

    xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
    if (!xpathCtx) {
        std::cerr << "Error: unable to create new XPath context\n";
        return "";
    }

    // Register namespaces if needed (often needed for modern HTML with microdata)
    // xmlXPathRegisterNs(xpathCtx, BAD_CAST "schema", BAD_CAST "http://schema.org/");
    // Add more namespaces if inspection reveals them

    std::stringstream relevantHtmlStream;

    // --- Define XPath Queries (Adjust these based on common recipe site structures) ---
    // Prioritize specific IDs/classes often used for recipes.
    // Then look for schema.org microdata.
    // Then look for broader article/main tags.
    const char* xpaths[] = {
        "//div[contains(@class, 'recipe-content')]",        // Common class patterns
        "//div[contains(@id, 'recipe')]",                  // Common ID patterns
        "//div[@itemscope and @itemtype='http://schema.org/Recipe']", // Schema.org Recipe
        "//article[contains(@class, 'recipe')]",           // Article tags
        "//main[contains(@class, 'recipe')]",              // Main tags
        "//div[contains(@class, 'ingredients')]",          // Specific ingredient sections
        "//div[contains(@class, 'instructions')]",         // Specific instruction sections
        "//div[contains(@class, 'directions')]",           // Specific direction sections
        // Fallback to broader containers if specific ones aren't found
        "//article",
        "//main"
    };

    bool foundSpecific = false;
    for (const char* xpathExpr : xpaths) {
        xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression(BAD_CAST xpathExpr, xpathCtx);
        if (!xpathObj) {
            std::cerr << "Error: unable to evaluate xpath expression \"" << xpathExpr << "\"\n";
            continue;
        }

        if (xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0) {
            std::cout << "XPath '" << xpathExpr << "' matched " << xpathObj->nodesetval->nodeNr << " nodes." << std::endl;
            foundSpecific = true; // Indicate we found something relevant

            // Dump the matched nodes (and their content) to the stringstream
            for (int i = 0; i < xpathObj->nodesetval->nodeNr; ++i) {
                xmlNodePtr node = xpathObj->nodesetval->nodeTab[i];
                xmlBufferPtr buffer = xmlBufferCreate();
                if (buffer) {
                    int dumpResult = xmlNodeDump(buffer, doc, node, 0, 1); // Dump node content
                    if (dumpResult >= 0) {
                        relevantHtmlStream << (const char*)xmlBufferContent(buffer) << "\n\n"; // Add separator
                    }
                    xmlBufferFree(buffer);
                }
            }
        }
        xmlXPathFreeObject(xpathObj);

        // Optimization: If we found specific recipe containers, maybe don't bother with broad article/main
        if (foundSpecific && (strstr(xpathExpr, "schema.org") || strstr(xpathExpr, "ingredients") || strstr(xpathExpr, "instructions"))) {
            // break; // Uncomment to stop after finding good specific tags
        }
        // Optimization 2: If we matched a broad container, don't match others below it?
        // if (foundSpecific && (strstr(xpathExpr, "//article") || strstr(xpathExpr, "//main"))) {
        //    break;
        // }
    }

    xmlXPathFreeContext(xpathCtx);

    std::string finalHtml = relevantHtmlStream.str();

    // If XPath failed to find anything specific, maybe return a smaller portion of original? (less ideal)
    // Or just return the (potentially empty) extracted content.
    // if (finalHtml.empty()) { /* maybe return original cleanHtml if small enough? */ }

    return finalHtml;
}

std::string cleanHtml(const std::string& rawHtml, const std::string& sourceUrl) {
    if (rawHtml.empty()) {
        return "";
    }
    htmlDocPtr doc = htmlReadMemory(rawHtml.c_str(), rawHtml.length(), sourceUrl.c_str(), NULL,
        HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    if (!doc) {
        std::cerr << "Libxml2 failed to parse HTML document from " << sourceUrl << std::endl;
        return ""; // Return empty on failure
    }

    std::string cleanedHtml = ""; // Initialize empty
    xmlNode* root_element = xmlDocGetRootElement(doc);
    if (root_element) {
        removeNodesByName(root_element, "script");
        removeNodesByName(root_element, "style");
        removeNodesByName(root_element, "script");
        removeNodesByName(root_element, "style");
        removeNodesByName(root_element, "header"); // Remove common noise
        removeNodesByName(root_element, "footer");
        removeNodesByName(root_element, "nav");
        removeNodesByName(root_element, "link");


        xmlChar* memBuffer = NULL;
        int sizeBuffer = 0;
        htmlDocDumpMemory(doc, &memBuffer, &sizeBuffer);

        if (memBuffer) {
            cleanedHtml.assign(reinterpret_cast<char*>(memBuffer), sizeBuffer);
            xmlFree(memBuffer);
        }
        else {
            std::cerr << "Libxml2 failed to dump cleaned HTML to memory for " << sourceUrl << std::endl;
        }
    }
    else {
        std::cerr << "Libxml2 failed to get root element for " << sourceUrl << std::endl;
    }

    std::string extractedHtml = extractPotentialRecipeHtml(doc);

    xmlFreeDoc(doc);

    std::cout << "Original cleaned size (approx): " << rawHtml.size() << ", Extracted relevant HTML size: " << extractedHtml.size() << std::endl;

    // Add a size limit? If extracted is still huge, maybe signal an issue?
    // const size_t MAX_REASONABLE_EXTRACT_SIZE = 100000; // 100KB example limit
    // if (extractedHtml.size() > MAX_REASONABLE_EXTRACT_SIZE) {
    //    std::cerr << "Warning: Extracted HTML is still very large (" << extractedHtml.size() << "), potential issue." << std::endl;
         // return "HTML_TOO_LARGE"; // Send specific signal?
    // }

    return extractedHtml;
}

void send_html_string_to_master(SOCKET sock, const std::string& html_content) {
    uint32_t dataSize = html_content.size();
    uint32_t netSize = htonl(dataSize); // Convert size to network byte order

    // 1. Send the size (4 bytes)
    int sizeSent = send(sock, reinterpret_cast<const char*>(&netSize), sizeof(netSize), 0);
    if (sizeSent != sizeof(netSize)) {
        std::cerr << "Failed to send data size. Error: " << WSAGetLastError() << std::endl;
        return;
    }
    std::cout << "Sent data size: " << dataSize << " bytes." << std::endl;


    // 2. Send the actual data (only if size > 0)
    if (dataSize > 0) {
        int totalSent = 0;
        int bytesLeft = dataSize;
        const char* ptr = html_content.c_str();

        while (bytesLeft > 0) {
            int bytesSent = send(sock, ptr + totalSent, bytesLeft, 0);
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "Send (data) failed with error: " << WSAGetLastError() << std::endl;
                return; // Exit function on send error
            }
            if (bytesSent == 0) {
                std::cerr << "Send (data) returned 0, connection possibly closed." << std::endl;
                return; // Exit function
            }
            totalSent += bytesSent;
            bytesLeft -= bytesSent;
        }
        std::cout << "Sent HTML data (" << totalSent << " bytes)." << std::endl;
    }
    else {
        std::cout << "Skipping send for 0-byte content." << std::endl;
    }
}


int main() {
    // --- Initialize libraries (outside the loop) ---
    xmlInitParser();
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) { /* ... error handling ... */ return 1; }

    // --- Setup Server Socket (outside the loop) ---
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) { /* ... error handling ... */ WSACleanup(); xmlCleanupParser(); return 1; }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8084); // Scraper Port

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) { /* ... bind error ... */ closesocket(serverSocket); WSACleanup(); xmlCleanupParser(); return 1; }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) { /* ... listen error ... */ closesocket(serverSocket); WSACleanup(); xmlCleanupParser(); return 1; }

    std::cout << "Scraper Slave ready. Waiting for master connections on port 8084..." << std::endl;

    // --- Main Accept Loop ---
    while (true) // <<< ADDED LOOP
    {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Scraper Slave: Accept failed: " << WSAGetLastError() << std::endl;
            // Decide if this is fatal. If accept keeps failing, maybe break.
            // For now, just log and try again (continue loop).
            continue;
        }
        std::cout << "\nScraper Slave: Master connected. Processing request..." << std::endl;

        // --- Processing Logic for ONE connection ---
        bool connectionActive = true;
        while (connectionActive) { // Inner loop to handle commands *within* one connection
            char buffer[4096];
            int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

            if (bytesReceived == SOCKET_ERROR) {
                std::cerr << "Scraper Slave: Recv failed: " << WSAGetLastError() << std::endl;
                connectionActive = false;
            }
            else if (bytesReceived == 0) {
                std::cout << "Scraper Slave: Master disconnected." << std::endl;
                connectionActive = false;
            }
            else {
                buffer[bytesReceived] = '\0';
                std::string receivedData(buffer);
                std::cout << "Scraper Slave: Received: '" << receivedData << "'" << std::endl;

                // Master sends URL then SEND_DATA
                // Assume first message is URL
                std::string url = receivedData;
                std::cout << "Scraper Slave: Processing URL: " << url << std::endl;

                std::string rawHtml = get_request(url);
                std::string cleanedHtml;
                if (!rawHtml.empty()) {
                    std::cout << "Scraper Slave: Cleaning HTML..." << std::endl;
                    cleanedHtml = cleanHtml(rawHtml, url);
                }
                else {
                    std::cerr << "Scraper Slave: Skipping cleaning due to fetch failure." << std::endl;
                    cleanedHtml = ""; // Send size 0 later
                }

                // Now wait for the SEND_DATA command
                std::cout << "Scraper Slave: Waiting for SEND_DATA command..." << std::endl;
                bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                if (bytesReceived > 0) {
                    buffer[bytesReceived] = '\0';
                    std::string command(buffer);
                    if (command == "SEND_DATA") {
                        std::cout << "Scraper Slave: Received SEND_DATA. Sending response..." << std::endl;
                        send_html_string_to_master(clientSocket, cleanedHtml); // Use the size prefix version here too!
                    }
                    else {
                        std::cerr << "Scraper Slave: Error: Expected SEND_DATA, got '" << command << "'" << std::endl;
                        connectionActive = false; // Protocol error, close connection
                    }
                }
                else {
                    // Error or disconnect while waiting for SEND_DATA
                    if (bytesReceived == 0) std::cout << "Scraper Slave: Master disconnected while waiting for SEND_DATA." << std::endl;
                    else std::cerr << "Scraper Slave: Recv failed waiting for SEND_DATA: " << WSAGetLastError() << std::endl;
                    connectionActive = false;
                }
                // Since master connects/disconnects per URL, we break the inner loop here
                connectionActive = false;
            }
        } // End inner command loop

        // --- Cleanup for this client ---
        std::cout << "Scraper Slave: Closing client socket." << std::endl;
        closesocket(clientSocket);
        std::cout << "Scraper Slave: Ready for next connection." << std::endl;

    } // --- End of while(true) Accept Loop ---


    // --- Global Cleanup (Might not be reached if loop is infinite) ---
    std::cout << "Scraper Slave shutting down." << std::endl; // Will only print if loop breaks
    closesocket(serverSocket); // Close listening socket
    WSACleanup();
    xmlCleanupParser();
    return 0;
}