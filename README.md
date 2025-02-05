# **Technical Documentation: FileQueueService**

## **Overview**

**FileQueueService** is a C++-based HTTP service built using **cpprestsdk (Casablanca)**, **SQLite3**, and **cURL**. It allows users to:

-   Upload **form-data** via an HTTP `POST` request.
-   Store the **form-data** in a file and maintain its path in an SQLite3 database.
-   Queue and periodically send the stored form-data to a **target server**.
-   Retrieve a list of queued files via an HTTP `GET` request.

The application is designed for **Raspberry Pi Zero 2W** and other Linux-based environments.

----------

## **Features**

-   Accepts **form-data** and stores it as a local file.
-   **Queues** form-data and forwards it to a target URL using `cURL`.
-   Allows dynamic **server host, port, and target server configuration**.
-   Provides a **GET API** to list all queued files in an **HTML table**.
-   Uses **SQLite3** for queue persistence.
-   Implements **multithreading** for queue processing.

----------

## **Installation & Dependencies**

### **🛠 Required Dependencies**

Ensure the following packages are installed:

```sh
sudo apt update
sudo apt install g++ cmake make sqlite3 libsqlite3-dev libcpprest-dev libcurl4-openssl-dev libssl-dev -y

```

### **🛠 Compile the Application**

```sh
g++ -std=c++17 -o FileQueueService FileQueueService.cpp -lcpprest -lsqlite3 -lcurl -lssl -lcrypto -pthread

```

### **🛠 Run the Application**

```sh
./FileQueueService --host 0.0.0.0 --port 8080 --target https://yourserver.com/upload

```

----------

## **Configuration**

### **🌍 Command-Line Arguments**

Argument

Description

`--host`

Server hostname (default: `0.0.0.0`)

`--port`

Server port (default: `8080`)

`--target`

Target URL for forwarding form-data (overrides environment variable)

### **🌍 Environment Variable Support**

-   If the `--target` flag is not provided, the system checks the environment variable `TARGET_SERVER`:

```sh
export TARGET_SERVER="https://yourserver.com/upload"

```

----------

## **API Endpoints**

### **📌 `POST /forward_form_data`**

**Description:** Receives form-data and stores it in a file.

-   **Request Format:** `multipart/form-data`
-   **Response:**
    -   `200 OK`: `"Form-data received and queued."`
    -   `500 Internal Server Error`: `"Failed to save form-data."`
    -   `400 Bad Request`: `"Empty form-data."`

**Example Request:**

```sh
curl --location 'http://localhost:8080/forward_form_data' \
--form 'file=@"/path/to/file.txt"'

```

----------

### **📌 `GET /forward_form_data`**

**Description:** Returns an **HTML table** with queued files.

-   **Response:** HTML page with a table of queued files.

**Example Request:**

```sh
curl http://localhost:8080/forward_form_data

```

----------


----------

## **🎯 Summary**

Feature

Description

**File Upload**

Saves form-data as a local file

**Queue Processing**

Periodically sends files to target server

**GET API**

Lists all queued files as an HTML table

**Dynamic Configuration**

Supports CLI args & environment variables

🚀 **Your C++ File Queue Service is now production-ready!** 🚀🔥
