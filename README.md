# Simple Proxy Server

This project is a basic proxy server implemented in C++, designed to handle both HTTP and HTTPS requests with a blacklist feature and a basic GUI.

## Project Structure

The project is organized into the following directories and files:

```
split_version/
    ├── include/
    │   ├── config.h      // Configuration constants and global variables
    │   ├── proxy.h       // Declarations for proxy logic functions
    │   ├── ui.h          // Declarations for user interface functions
    │   ├── utils.h       // Declarations for utility functions
    │
    ├── src/
    │   ├── config.cpp    // Definitions of global variables
    │   ├── main.cpp      // Main entry point of the application
    │   ├── proxy.cpp     // Implementation of proxy server logic
    │   ├── ui.cpp        // Implementation of the user interface
    │   ├── utils.cpp     // Implementation of utility functions
    ├── README.md         // This file
```

### `include/` Directory

- **`config.h`**: This header file defines global constants and extern declarations of the global variables such as the port, buffer size, max log entries, and all the UI windows. It also contains the `running` flag and mutexes for thread-safe operation.
- **`proxy.h`**: This header file declares the functions responsible for the proxy server's core logic, including handling HTTP and HTTPS requests and managing client connections.
- **`ui.h`**: This header file declares the functions responsible for the user interface, including the `WndProc` and `WinMain` entry points.
- **`utils.h`**: This header file declares utility functions used across the project such as logging, resolving hostnames, blacklist checking, parsing host headers, and adding/removing blacklist URLs.

### `src/` Directory

- **`config.cpp`**: This file contains the definitions of the global variables that are declared in `config.h`.
- **`main.cpp`**: This file is the main entry point of the Windows GUI application. It calls the `WinMain` function which is declared in `ui.h` and implemented in `ui.cpp` .
- **`proxy.cpp`**: This file implements the core proxy server logic, including:
  - Handling `CONNECT` requests (HTTPS).
  - Handling `GET` and `POST` requests (HTTP).
  - Blacklist checking.
  - Resolving hostnames to IP addresses.
- **`ui.cpp`**: This file implements the application's user interface (window, buttons, listboxes). It uses WinAPI and includes the following responsibilities
  - Setting up the main window and its controls.
  - Handling user interactions with the buttons, input box, listbox.
  - Contains the Windows message loop.
- **`utils.cpp`**: This file contains the implementation of functions for:
  - Logging to files and UI windows.
  - Parsing the host header.
  - Resolving hostnames.
  - Checking the blacklist.
  - Adding/removing URLs to/from the blacklist.

## How to Run the Proxy Server

Follow these steps to build and run the proxy server:

### Prerequisites

- **C++ Compiler**: You'll need a C++ compiler that supports C++11 or later and is compatible with Windows, such as g++ (MinGW), or Visual Studio.
- **Windows Operating System**: The project utilizes the Windows API, so it must run on a Windows environment.
- **Winsock library**: The project uses `winsock2.h` so the linker flag `-lws2_32` is required

### Steps

1.  **Clone or Download:** Clone or download the project files to your local machine.

2.  **Navigate to the Project:** Open a terminal or command prompt and navigate to the root directory of the project where `src` and `include` folders exist.

3.  **Compile the Code:** Use the following command to compile the code with `g++`:

    ```bash
    g++ -o proxy src/*.cpp -I include -lws2_32 -mwindows
    ```

    - `-o proxy`: Specifies the output executable name.
    - `src/*.cpp`: Specifies that all `.cpp` files in the `src` directory are to be compiled.
    - `-I include`: Specifies that the `include` directory is to be included when compiling
    - `-lws2_32`: Links the Winsock library.
    - `-mwindows`: Specifies that it's a Windows GUI application

4.  **Run the Executable:** After successfully compiling, run the executable file using:

    ```bash
    proxy.exe
    ```

    This will start the proxy server application.

5.  **Using the GUI:**
    - Click the **"Start"** button to begin the proxy server on port `8888`.
    - Enter URLs in the input box and click **"Add URL"** or press **Enter** to add them to the blacklist.
    - Select an URL from the blacklist and click **"Remove"** to remove the URL.
    - Click **"Stop"** to stop the proxy server.
    - The connection log appears on the log window.
    - The list of clients connected to the proxy server appears in the client window.
    - The list of hostnames the clients currently access appears in the running host window.

### Notes

- To start using the proxy in your system:
  - Go to settings > Proxy
  - Turn on use a proxy server
  - Set address to `127.0.0.1` and port to `8888`
- If the program cannot start, check if other programs are using port `8888` and try other ports or close the program using the specified port
- The proxy server logs connections and requests to the `log-[day]-[month]-[year].txt` file.

## Contributing

If you'd like to contribute to this project, feel free to open a pull request with your changes or report bugs or issues in the issues section.
