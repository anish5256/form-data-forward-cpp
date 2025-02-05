#define _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
#define _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS
#define _CRT_SECURE_NO_WARNINGS



#include <cpprest/http_listener.h>
#include <cpprest/containerstream.h>
#include <cpprest/filestream.h>
#include <cpprest/json.h>
#include <cpprest/http_headers.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <sqlite3.h>
#include <sys/stat.h>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>  // ✅ Windows-specific mkdir
#define MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0777)
#endif

using namespace web;
using namespace web::http;
using namespace web::http::experimental::listener;
using namespace concurrency::streams;
using namespace std;

const string UPLOAD_DIR = "./uploads/";
const string DB_FILE = "files.db";
string TARGET_SERVER = "";  // Default value

string SERVER_HOST = "0.0.0.0";
int SERVER_PORT = 8080;


// Ensure the upload directory exists
void ensure_upload_dir() {
    struct stat info;
    if (stat(UPLOAD_DIR.c_str(), &info) != 0) {
        cout << "Creating upload directory..." << endl;
        MKDIR(UPLOAD_DIR.c_str());  // ✅ Cross-platform mkdir
    }
}

// Store file path in the database
void save_file_path(const string& file_path) {
    sqlite3* db;
    if (sqlite3_open(DB_FILE.c_str(), &db) != SQLITE_OK) {
        cerr << "Error opening database: " << sqlite3_errmsg(db) << endl;
        return;
    }

    string sql = "INSERT INTO files (path) VALUES (?);";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        cout << "Stored file path in database: " << file_path << endl;
    }
    else {
        cerr << "Error inserting file path: " << sqlite3_errmsg(db) << endl;
    }

    sqlite3_close(db);
}

// Get pending files from database
vector<string> get_pending_files() {
    vector<string> files;
    sqlite3* db;

    if (sqlite3_open(DB_FILE.c_str(), &db) != SQLITE_OK) {
        cerr << "Error opening database: " << sqlite3_errmsg(db) << endl;
        return files;
    }

    string sql = "SELECT path FROM files;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            files.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return files;
}

// Delete file from database
void delete_file_entry(const string& file_path) {
    sqlite3* db;

    if (sqlite3_open(DB_FILE.c_str(), &db) != SQLITE_OK) {
        cerr << "Error opening database: " << sqlite3_errmsg(db) << endl;
        return;
    }

    string sql = "DELETE FROM files WHERE path = ?;";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, file_path.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}

// Send form-data to a remote server
bool send_form_data(const string& file_path) {
    ifstream file(file_path, ios::binary);
    if (!file) {
        cerr << "Error opening file: " << file_path << endl;
        return false;
    }

    string form_data((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    file.close();

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: multipart/form-data");

    curl_easy_setopt(curl, CURLOPT_URL, TARGET_SERVER.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_data.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    return res == CURLE_OK;
}

// **Queue Processing: Send and Delete**
void process_queue() {
    while (true) {
        vector<string> pending_files = get_pending_files();

        for (const string& file_path : pending_files) {
            if (send_form_data(file_path)) {
                remove(file_path.c_str());  // ✅ Delete file after sending
                delete_file_entry(file_path);  // ✅ Remove from DB
                cout << "Successfully sent & deleted: " << file_path << endl;
            }
            else {
                cerr << "Failed to send: " << file_path << endl;
            }
        }

        this_thread::sleep_for(chrono::seconds(10));  // ✅ Process queue every 10s
    }
}

// **Save raw form-data to a file**
string save_form_data_to_file(const vector<uint8_t>& raw_data) {
    string file_name = UPLOAD_DIR + "formdata_" + to_string(time(0)) + ".txt";
    ofstream file(file_name, ios::binary);

    if (file) {
        file.write(reinterpret_cast<const char*>(raw_data.data()), raw_data.size());
        file.close();
        cout << "Saved raw form-data in file: " << file_name << endl;
        return file_name;
    }
    else {
        cerr << "Error saving form-data file: " << file_name << endl;
        return "";
    }
}

// **Main API handler for file uploads**
void handle_post(http_request request) {
    ensure_upload_dir();  // Ensure uploads directory exists

    request.content_ready().then([=](http_request req) {
        concurrency::streams::container_buffer<vector<uint8_t>> buffer;

        return req.body().read_to_end(buffer).then([=](size_t bytes_read) {
            if (bytes_read == 0) {
                request.reply(status_codes::BadRequest, U("Empty form-data."));
                return;
            }

            // **Extract raw binary data**
            vector<uint8_t> raw_data = buffer.collection();

            // **Save form-data to a file**
            string file_path = save_form_data_to_file(raw_data);

            if (!file_path.empty()) {
                save_file_path(file_path);  // ✅ Store only file path in DB
                request.reply(status_codes::OK, U("Form-data received and queued."));
            }
            else {
                request.reply(status_codes::InternalError, U("Failed to save form-data."));
            }
            });
        }).wait();
}

// **Set TARGET_SERVER from Args or Env**

void configure_server(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];

        if (arg == "--host" && i + 1 < argc) {
            SERVER_HOST = argv[++i];  // ✅ Set custom host
        }
        else if (arg == "--port" && i + 1 < argc) {
            SERVER_PORT = stoi(argv[++i]);  // ✅ Set custom port
        }
        else if (arg == "--target" && i + 1 < argc) {
            TARGET_SERVER = argv[++i];  // ✅ Fix: Set target correctly
        }
    }

    // Check if environment variable TARGET_SERVER is set (only if not set via CLI)
    if (TARGET_SERVER == "--target" || TARGET_SERVER.empty()) {  // ✅ Fix: Ensure `--target` is not used as a value
        char* env_url = getenv("TARGET_SERVER");
        if (env_url != nullptr) {
            TARGET_SERVER = string(env_url);  // ✅ Use environment variable if available
        }
    }

    // Ensure TARGET_SERVER is set
    if (TARGET_SERVER.empty() || TARGET_SERVER == "--target") {
        cerr << "❌ Error: Target Server is not set. Exiting now." << endl;
        exit(1);
    }

    cout << "🚀 Server Host: " << SERVER_HOST << endl;
    cout << "🚀 Server Port: " << SERVER_PORT << endl;
    cout << "🚀 Target Server: " << TARGET_SERVER << endl;
}



void init_db() {
    sqlite3* db;
    if (sqlite3_open(DB_FILE.c_str(), &db) != SQLITE_OK) {
        cerr << "Error opening database: " << sqlite3_errmsg(db) << endl;
        exit(1);
    }

    string sql = "CREATE TABLE IF NOT EXISTS files (id INTEGER PRIMARY KEY, path TEXT NOT NULL);";
    if (sqlite3_exec(db, sql.c_str(), 0, 0, 0) != SQLITE_OK) {
        cerr << "Error creating table: " << sqlite3_errmsg(db) << endl;
        exit(1);
    }

    sqlite3_close(db);
}


void handle_get(http_request request) {
    vector<string> queued_files = get_pending_files();

    // Use explicit std::stringstream to avoid ambiguity
    std::stringstream html;
    html << "<html><head><title>Queued Files</title></head><body>";
    html << "<h2>Queued Files</h2>";
    html << "<table border='1' cellpadding='5' cellspacing='0'>";
    html << "<tr><th>#</th><th>File Path</th></tr>";

    for (size_t i = 0; i < queued_files.size(); i++) {
        html << "<tr><td>" << (i + 1) << "</td><td>" << queued_files[i] << "</td></tr>";
    }

    html << "</table></body></html>";

    // Send response as HTML
    request.reply(status_codes::OK, html.str(), "text/html");
}



// **Server Entry Point**
int main(int argc, char* argv[]) {
    configure_server(argc, argv);
    init_db();

    string url = "http://" + SERVER_HOST + ":" + to_string(SERVER_PORT) + "/forward_form_data";

    // ✅ Ensure the URL is correctly converted to utility::string_t
    http_listener listener(utility::conversions::to_string_t(url));

    //http_listener listener(U(url));
    listener.support(methods::POST, handle_post);
    listener.support(methods::GET, handle_get);
    listener.open().wait();

    cout << "Ok Server started at "<<url << endl;

    thread queue_thread(process_queue);  // ✅ Start queue processing
    queue_thread.detach();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));  // ✅ Sleep for 0.5s
    }

    return 0;
}
