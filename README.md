# Eco3DPrint
*Internet of Things, a.y. 2025/2026*

*Univeristy of Pisa,*
*Department of Information Engineering,*
*m.sc. Artificial Intelligence and Data Engineering*

*Project by Francesco Panattoni*

## Initial Recommendation
The ***cooja*** branch contains the version of the project designed to run on the Virtual Machine provided during the Internet of Things course. The ***main*** branch, instead, includes a containerized setup that allows the project to be run locally. 

*If you are a student at the University of Pisa looking for reference material or inspiration, the cooja branch may be useful. Otherwise, if you are not enrolled in the course or prefer to run the project in a containerized environment, refer to the main branch.*

## Installation
Put the Eco3DPrint folder project in /home/"user"/contiki-ng/examples (for the students of Unipi /home/iot_ubuntu_intel/contiki-ng/examples). Then open the terminal and type:
```bash
./setup.sh -setup
```

To make the LED work correctly on Cooja go to *contiki-ng/arch/platform/cooja/contiki-conf.h* and on the bottom modify this:
```bash
/* Virtual LED colors */

#define LEDS_CONF_COUNT                  3

#define LEDS_CONF_GREEN                  1

#define LEDS_CONF_RED                    2

#define LEDS_CONF_YELLOW                 4
```
into this:
```bash
/* Virtual LED colors */

#define LEDS_CONF_COUNT                  3

#define LEDS_CONF_GREEN                  0

#define LEDS_CONF_RED                    1

#define LEDS_CONF_YELLOW                 2
```

## Starting the Application
To start the Application:
- Open a terminal in the project folder and run:
  ```bash
  ./setup.sh -sim
  ```
- A new terminal will open with Cooja. In Cooja:
  - Load the file `cooja/Cooja-Simulation.csc`
  - Verify that the simulation opens correctly
- Return to the **main terminal** (where you ran `./setup.sh -sim`) and:
  - Press any key to start the Border Router
- Return again to the **main terminal** and:
  - Press any key to start the Mosquitto Server
- Finally, return to the **main terminal** and:
  - Press any key to start the Cloud Application Server and User App
- Remember to click the button on all the motes to put start the initialization. 

## Introduction
The advent of **3D Printing** has revolutionized manufacturing, prototyping, and personal fabrication by allowing digital models to be transformed into physical objects layer by layer. However, the additive **manufacturing process is inherently susceptible to physical anomalies**. A slight deviation in temperature, extrusion rate, or mechanical calibration can lead to catastrophic print failures. When a printer operates unmonitored for extended periods, these undetected failures result in a significant waste of filament materials, excessive energy consumption, and lost time.

To mitigate this inefficiencies, we must look toward modern networking paradigms. The **Internet of Things** (IoT) represents the evolution of the network that extends connectivity to physical objects of daily life. By transforming standalone, "dumb" 3D printers into smart, interconnected assets, we can actively monitor their health and intervene dynamically before resources are wasted.

This report presents ***<u>Eco3DPrint</u>***, *an intelligent monitoring application designed to safeguard the 3D printing process*. An IoT system is composed of four functional levels: devices and sensors that collect data, connectivity that allows data to travel, data processing on the cloud, and a user interface. The architecture is inspired to the "*Comprehensive Review on Internet of Things Applications in Power Systems*" [1](#ref1).

Our solution fully embodies this architecture. We deploy specialized, network-connected dongles equipped with accelerometers and voltage sensors directly onto the printers. As the machine operates, these sensors continuously monitor the physical dynamics of the extruder and the build plate, alongside the power drawn by the system. The collected data is fed into a localized *Machine Learning (ML) model* trained to distinguish between normal operational vibrations and the erratic, anomalous patterns indicative of a failing print.

Upon detecting an anomaly, the system automatically halts the printing process to prevent further material and energy waste. Users manage the entire ecosystem through a comprehensive software suite (a *Cloud backend* and a *User Application frontend*) which facilitates the direct transfer of STL files to the machines via the network. Furthermore, *the application provides transparent, analytical tracking of daily, weekly, and monthly energy consumption, explicitly differentiating between "well-used" energy (successful prints) and "wasted" energy (failed attempts)*.

From a technical standpoint, deploying **constrained embedded devices** at the edge of the network is a deliberate and crucial architectural choice. While a high-powered, general-purpose computer could theoretically monitor a printer, this application aligns with the principles of the *Industrial Internet of Things (IIoT)*, where efficiency and scalability are paramount.

Using constrained devices enables cost-effective retrofitting, allowing existing 3D printers without built-in monitoring to be integrated into an IoT ecosystem through inexpensive embedded dongles, avoiding costly hardware replacement. It also improves network efficiency by processing high-frequency telemetry locally thereby reducing bandwidth usage while relying on lightweight protocols such as *CoAP* and *MQTT* instead of heavier alternatives like *HTTP*. 

We use .stl files instead of .gcode or .pws, as they are more readily available online and generally smaller to transfer. Although this bypasses the initial slicing step, it does not affect simulation and requires minimal code adaptation.  

## Environment
### Simulation
This project is developed as part of an academic simulation within the Internet of Things course, and is therefore designed to operate within the controlled environment and assumptions defined by the professors, which are outlined briefly below. A Docker container has been provided to make it work even for those who do not have a virtual machine, but this does not detract from the simulation purpose.

### Contiki-NG
**Contiki-NG** [2](#ref2) represents a sophisticated, open-source operating system explicitly architected for resource-constrained embedded devices within the Internet of Things ecosystem.

At the foundation of its architecture lies a highly modular, event-driven kernel that operates utilizing Protothreads. This specialized programming model facilitates memory-efficient cooperative multithreading without incurring the heavy overhead typically associated with per-thread stacks, thereby allowing the entire operating system to function optimally within remarkably tight hardware constraints, often requiring as little as 10 kilobytes of random-access memory and a total code footprint of approximately 100 kilobytes.

### Cooja
**Cooja** is an advanced, Java-based open-source network simulator inherently integrated within the Contiki and Contiki-NG ecosystems, specifically engineered to emulate the intricate dynamics of IoT and wireless sensor networks. It supports cross-level simulation, allowing analysis from network topology down to operating system behavior and machine code execution. It can emulate hardware using tools like MSPSim, enabling real embedded code to run as it would on devices such as MSP430-based nodes.

Cooja offers different mote types for testing, but fast "Cooja motes" are used for high-level simulation and are emulated for detailed hardware-level validation, including drivers and power management. With a graphical interface for visualizing networks, timelines, and serial output, it helps developers analyze radio interactions, debug protocols, and optimize systems before deploying on real hardware.

### Tunslip
**Tunslip** is a utility included in Contiki-NG that establishes a virtual SLIP (Serial Line Internet Protocol) link between a border router and a host computer. It is primarily used to bridge an IoT network with the host’s networking stack by creating a tunnel interface.

Through this virtual connection, IPv6 packets generated within a simulated or physical sensor network can be forwarded to and from external networks, including the internet. This makes it possible for constrained IoT nodes to communicate with standard IP-based systems as if they were part of the same routed network, facilitating testing and integration of networked applications.

### Nordic Dongle NRF52840
The **nRF52840 Dongle** is a compact, low-cost USB development platform from *Nordic Semiconductor* built around the nRF52840 SoC. It uses a 32-bit ARM Cortex-M4 processor with a hardware FPU running at 64 MHz and includes 1 MB of Flash and 256 KB of RAM for embedded applications.

It supports multiple 2.4 GHz wireless protocols, including Bluetooth 5 (BLE, long range, 2 Mbps, and mesh), IEEE 802.15.4 for Thread and Zigbee, as well as ANT and proprietary protocols. Security is handled by the integrated CryptoCell-310 hardware accelerator for cryptographic operations.

The board is USB-powered, operates from 1.7 V to 5.5 V, and includes a user button, LEDs, and 15 GPIOs accessible via edge pads. It can be programmed and updated via USB and integrates with the nRF Connect for Desktop ecosystem without requiring an external debugger.

## Architecture
### Ecosystem
The system is composed of an **IoT Network** and a **Cloud Application**.

The *IoT Network* architecture is based on a decoupled system model consisting of a **Border Router** and a collection of physical edge devices that form a coordinated local pairing: **Actuator Nodes** (Printers) and **Sensor Nodes**.

The *Cloud Application* serves as the central orchestration hub of the ***Eco3DPrint* ecosystem**, coordinating communication between the distributed hardware nodes and the **User App interface**.

### IoT Network
#### Nodes
Rather than employing a single device to execute both physical simulation and telemetry collection, the edge node architecture is split into two specialized typologies:

* **Actuator Nodes (Smart Printers):** These nodes execute the primary 3D printer state machine, orchestrating the system across various phases including initialization, network alignment, and simulated printing cycles. They act as CoAP servers to handle inbound administrative traffic from the Cloud Application backend, using application-layer block-wise mechanisms to ingest large binary STL files. Crucially, the Actuator Node hosts the localized machine learning inference engine; it dynamically subscribes to its paired sensor's telemetry feed, extracts multi-axis statistical features from incoming data windows, and autonomously cuts power to the printing process if consecutive anomalies are predicted.
* **Sensor Nodes (Smart Sensors):** These nodes act as a digital sensory twin, isolating data acquisition tasks from mechanical control loops. Operating under a distinct state machine, they mathematically simulate physical telemetry streams (acceleration variations, filament structural tension metrics, and real-time power consumption) using Gaussian profiles. The Sensor Node formats these streams into standardized SenML-compliant JSON arrays and transmits them over a dedicated MQTT telemetry plane. To manage constrained radio resources, it implements a strict batching mechanism, pausing its internal sampling timer after every 5 messages until a CoAP validation command (`CONT` or `STOP`) is received from its paired printer.

<figure id="fig:Protocol-Stack" data-latex-placement="H">
<img src="doc/img/Protocol_Stack.png" style="width:80.0%" />
<figcaption>Personalized Protocol Stack of the App</figcaption>
</figure>

### Cloud Application
#### Backend
The **Cloud Application backend** is built on a multi-threaded Python architecture, and it bridges the resource-constrained IoT network with the graphical User Application (the frontend). The backend is strictly **modular**, separating network communication, business logic, and data persistence into dedicated functional components.
- **Database Access Layer:** Manages persistent data storage in a MySQL database, utilizing Connection Pooling to safely handle concurrent, thread-safe read and write operations;
- **CoAP Server:** Runs over UDP/IPv6 to handle low-frequency control events, including node registration, end-of-print notifications, and shutdown signals. It is implemented using **CoAPthon** [3](#ref3);
- **MQTT Handler:** Runs as a background subscriber to continuously ingest high-frequency JSON telemetry (vibration and voltage data) during active prints, mapping the data directly to the database. It is implemented using **Paho** [4](#ref4);
- **Node Monitor:** Maintains real-time fleet health by concurrently pinging active nodes via CoAP /health requests every 60 seconds. It utilizes an Observer pattern to instantly broadcast state changes (*OFFLINE*, *ONLINE*, *PRINTING*) across the system;
- **Print Manager:** Orchestrates the print job queue and handles the reliable transmission of 3D models. It utilizes CoAP Block-wise transfer to partition and sequentially send large binary STL files to available nodes;
- **WebSocket Manager:** It pushes live node status updates to the User App’s dashboard and processes complex analytical requests to generate daily, weekly, and monthly energy reports.

#### Frontend
The **Frontend Application** (**User App**) is a desktop-based graphical interface built using the Python **tkinter framework** [5](#ref5). A background daemon thread runs an asyncio event loop for a persistent, non-blocking WebSocket. Messages move through a thread-safe queue to the main GUI, which polls and routes them to the appropriate screen based on their type:
- **3D Printers Dashboard:** Displays a responsive grid of interactive cards for real-time fleet management. It dynamically applies color filters to printer icons to reflect current operational states (*OFFLINE*, *ONLINE*, *PRINTING*) and includes modal dialogs for viewing node details and queueing STL files;
- **Daily Report:** Visualizes the current day’s energy metrics, automatically converting raw Joules to kWh. It features high-level global summary cards and scrollable tables (ttk.Treeview) that break down well-used versus wasted energy by individual printer and STL file;
- **Weekly Report:** Aggregates telemetry over the week. It implements a zero-padded data structure to ensure consistent daily rendering and utilizes multiple centered tables to highlight energy consumption trends throughout the week;
- **Monthly Report:** Provides a long-term analytical view with a Year-To-Date (YTD) summary. It displays a month-by-month historical table tracking total print volume, successful energy usage, and energy lost to anomalies.

### Project Alignment
#### Project Objective
> **Smart Homes and Buildings:** enabling real-time control and energy efficiency in residential or commercial environments.

I choose "*Smart Homes and Buildings*" as a domain for my project. I have enabled a **real-time monitoring application** where I monitor the status of the printers, create a list of STLs to print in real time and monitor the energy consumption of the printers. All of this is **designed to monitor energy efficiency**, supported by a **Machine Learning model** that proactively halts faulty prints before they occur. This system is primarily intended for **commercial environments** where multiple 3D printers operate simultaneously, such as print farms or production labs, where efficiency, reliability, and cost control are critical. However, it is equally applicable in advanced home setups with multiple 3D printers, offering the same benefits of centralized monitoring, energy optimization, and failure prevention on a smaller scale.

#### Application Protocol
The architecture implements a hybrid application layer utilizing both **CoAP** and **MQTT** to establish a clear distinction between the control and telemetry planes.

**CoAP** serves as the primary mechanism for all control-plane operations, spanning both cloud-to-device and device-to-device interactions. CoAP was selected over MQTT for discrete control tasks due to the following specific reasons:
- **RESTful Interaction for Cloud Management:** Unlike the publish-subscribe model of MQTT, CoAP is a request-response protocol operating over lightweight UDP, allowing Actuator nodes to execute cloud registration without maintaining a persistent, energy-intensive connection.
- **Reliable Block-Wise File Transfer:** CoAP inherently supports Block-wise transfers (Block1), uniquely optimizing it for sending large binary assets like STL models one fragment at a time, preventing RAM exhaustion.
- **Reduced Protocol Overhead and Local Pacing:** By eliminating the TCP three-way handshake, CoAP minimizes radio transmissions. Crucially, the decoupled Actuator and Sensor nodes utilize local CoAP resources for asynchronous pairing discovery and for a strict "Ping-Pong" control loop, where the Actuator sends low-overhead commands (`START`, `CONT`, `STOP`) to dynamically pace or halt the sensor's sampling states based on ML inference cycles.

In contrast, **MQTT** is utilized exclusively to manage the high-frequency telemetry plane during active printing phases, operating as a distributed publish-subscribe link. 

A **Publish–Subscribe model is more efficient for this telemetry plane than a Request–Response approach**. The Sensor node functions as a dedicated publisher, streaming serialized profiles to an IP-specific topic (`<sensor_ip>/print/measurements`) using QoS 1. The Actuator node acts as a local subscriber, continuously ingesting this JSON telemetry stream directly from the broker to feed its localized sliding-window Machine Learning model.

#### Edge Intelligence and Autonomous Decision-Making
The system executes a localized **Machine Learning model** directly on the constrained **Actuator node**, enabling real-time, autonomous responses to mechanical anomalies without relying on cloud computation. The high-frequency data acquisition is isolated on the **Sensor node**, which streams raw operational metrics over the local network. The Actuator ingests these streams from the broker and executes a C-based pipeline to perform edge feature extraction, computing 35 statistical parameters across a localized sliding window. By processing these computations locally, the system drastically reduces cloud bandwidth consumption and ensures that the Actuator can autonomously halt the printing process and send an immediate, direct CoAP shutdown signal to the Sensor even during external internet connectivity loss.

#### Hardware Interaction
The project strictly adheres to the mandatory button and LED interaction requirements.

A **physical button** is used for manual overrides, system resets, and confirming print completions, ensuring a human-in-the-loop safety mechanism.

At the same time, **LEDs** provide essential visual feedback through color coding, yellow for initialization, green for online status, and red for failure, allowing an on-site operator to quickly assess device status without a display.

#### Data Encoding
**JSON**, structured via the **SenML** (Sensor Measurement Lists [6](#ref6)) data model, is the optimal encoding choice because its minimalist key-value structure drastically reduces packet overhead compared to the verbose tagging required by XML. This standardized approach facilitates a seamless data pipeline between the C-based firmware and the Python backend without the heavy memory or CPU requirements of an XML DOM parser.

For the transmission of STL models, the system implements **CoAP Block-wise transfer** to overcome hardware link limitations by partitioning binary data into sequential 64-byte chunks. This mechanism ensures reliable delivery through application-layer acknowledgments and prevents RAM exhaustion on the constrained nRF52840 nodes by processing only one fragment at a time.

## Implementation
### Actuator Node
The code related to the IoT devices has been placed inside the subfolder *Eco3DPrint/src/Printer3D/Actuator*.

The physical edge node (nRF52840 Dongle) acts as an autonomous computing hub representing the Actuator component of the decoupled architecture. Rather than interacting directly with physical hardware pins or hosting localized data-collection loops, the Actuator functions as a centralized control and inference point that ingests high-frequency operational telemetry streamed by a paired remote Sensor node over an active network link. The firmware is built on **Contiki-NG** and is modularly structured to separate the localized state transitions from underlying network and analytical peripherals.

The `device.c` houses the main Contiki-NG process (*PROCESS_THREAD*) and manages the primary control loop through a strict state machine, transitioning between `STATE_OFF`, `STATE_INITIALIZATION`, `STATE_ONLINE`, `STATE_PRINTING`, and `STATE_OFFLINE` to govern node behavior and optimize energy usage.

For the purposes of this project, we don’t care if the 3D Printer can take prints locally, so the state machine works like this:

<figure id="fig:Actuator-State-Machine" data-latex-placement="H">
<img src="doc/img/Actuator_State_Diagram.png" style="width:90.0%" />
<figcaption>Actuator State Diagram</figcaption>
</figure>

The node starts in `STATE_OFF` and moves to `STATE_INITIALIZATION` after a physical button press, triggering local pairing routines alongside a primary cloud registration. Upon initialization, the Actuator utilizes an asynchronous CoAP discovery process to identify a nearby Sensor node over the link layer. **Sensor discovery is statically configured**: the actuator is preconfigured with the IP address of its paired sensor and does not perform dynamic discovery or proximity-based sensor selection. If cloud registration fails, it enters `STATE_OFFLINE` and retries periodically via a dedicated timer loop until a valid acknowledgment is received, subsequently transitioning to `STATE_ONLINE`. In this state, the Actuator listens for incoming CoAP server traffic and, upon receiving a complete STL file via application-layer block-wise transfers, dynamically calculates a simulated printing duration and transitions into `STATE_PRINTING`.

During active printing, the node executes real-time MQTT telemetry subscription handling and embedded machine learning inference routines. It returns to `STATE_ONLINE` when a print job finishes successfully or when consecutive neural network anomalies force an early abort. A long button press (**5+ seconds**) from any active state forces a safe disconnect, transmitting a termination signal to the cloud, unpairing the linked sensor, and resetting the entire device to `STATE_OFF`.

During active prints, data acquisition is entirely network-driven rather than hardware-timer-bound. The Actuator concurrently listens for incoming messages on its subscribed telemetry channel (`<sensor_ip>/print/measurements`), using a custom string-parsing utility (`extract_json_value`) to decode SenML JSON frames containing acceleration, tension, and power values transmitted by the remote sensor. These values are stored inside an internal matrix structure (`sensor_buffer[8][5]`), building an overlapping data window.

For edge AI and inference, as soon as a window of 5 sequential samples is completed, the processing pipeline calculates 35 statistical features (computing the mean, standard deviation, minimum, maximum, and peak-to-peak metrics across the variables). The features are then standardized using auto-generated pre-calculated constants (`SCALER_MEANS` and `SCALER_SCALES`) and evaluated by the embedded neural network via the `print_prediction_predict()` function.

If the model predicts a failure for three consecutive cycles, the Actuator executes autonomous fault handling: it immediately aborts the print process, terminates local execution timers, pushes a direct CoAP `"STOP"` payload to suspend the Sensor node’s active sampling threads, and flashes a physical RED LED warning. If the machine learning verification succeeds, the Actuator executes a sliding-window operation, moving the last sample to the first position of the buffer to retain 1 common overlapping reading between windows, and transmits an asynchronous CoAP `"CONT"` command to prompt the Sensor for the next batch.

Once printing is complete or interrupted by an anomaly detection, the Actuator stays in a verification lock (`waiting_for_confirmation = true`). To report final operational outcomes to the cloud backend, the system implements a multi-duration button interaction mechanism on the board:

- **Short Press ( **\<** **2** seconds):** Confirms the currently recorded print state, triggering a blocking CoAP POST request to the `/print/finished` endpoint to transmit total aggregated energy consumption and state details to the cloud backend before reverting to `STATE_ONLINE`;

- **Medium Press (2–4 seconds):** Functions as a manual supervisor override. If a print job was aborted due to an ML false positive, a 2–4 second hold clears the error logs, restores the remaining print time, establishes a safe resubscription path to the MQTT broker, and transmits a CoAP `"START"` payload to wake the Sensor node back into active sampling. Conversely, if a job finished normally but suffered unpredicted physical defects, a 2–4 second hold overrides the successful verdict to report a failure instead.

<figure id="fig:LED-STATE" data-latex-placement="H">
<img src="doc/img/Actuator_Led.png" style="width:80.0%" />
<figcaption>LED for all the states</figcaption>
</figure>

To keep the main application loop clean, peripheral functionalities are isolated across dedicated compilation modules:
- `device.c`: Manages the global Contiki-NG processes, the primary state machine logic, network-driven buffer aggregation, statistical feature extraction routines, and manual user override flows;
- `coap_module.c`: Orchestrates the UDP control plane, processing local asynchronous discovery pairing hooks with the Sensor (`printer/discovery`), handling local sensor pacing commands (`"START"`, `"CONT"`, `"STOP"`), and managing cloud resource registration handshakes;
- `mqtt_module.c`: Governs the connection state with the TCP MQTT broker and handles dynamic subscriptions to the specific sensor’s topic feed during active prints;
- `print_prediction.h`: Contains the compiled structural arrays of the multi-layer neural network, providing embedded classification functionality via emlearn configurations;
- `scaler_params.h`: Stores the pre-calculated scaling averages and dispersion factors required to standardize live metrics prior to neural network inference execution.

### Sensor Node
The code related to the IoT devices has been placed inside the subfolder *Eco3DPrint/src/Printer3D/Sensor*.

The physical edge node (nRF52840 Dongle) acts as a digital sensory twin representing the data acquisition component of the decoupled architecture. Rather than executing mechanical control logic, the Sensor functions as a dedicated telemetry publisher that mathematically simulates physical environments and streams high-frequency operational data to the paired Actuator node over an active network link. The firmware is built on **Contiki-NG** and is modularly structured to separate the localized state transitions and simulation math from the underlying network protocols.

The `sensors_main.c` houses the main Contiki-NG processes (*PROCESS_THREAD*), managing both the primary control loop and a parallel health-checking routine. It governs node behavior through a strict state machine, transitioning between `STATE_OFF`, `STATE_INIT`, `STATE_SLEEP`, and `STATE_ACTIVE` to optimize bandwidth and energy usage.

For the purposes of this project, the sensor state machine works like this:

<figure id="fig:Sensor-State-Machine" data-latex-placement="H">
<img src="doc/img/Sensor_State_Diagram.png" style="width:90.0%" />
<figcaption>Sensor State Diagram</figcaption>
</figure>

The node starts in `STATE_OFF` and moves to `STATE_INIT` after a physical button press, triggering the network initialization and pairing phase. Upon initialization, the Sensor utilizes an asynchronous CoAP discovery process to locate an Actuator over the local network. To function robustly within a simulated Dongle environment, the discovery routine iteratively pings a hardcoded list of known Dongle IPs (`known_dongle_ips`) until a valid pairing acknowledgment is received. Once successfully paired, the device transitions to `STATE_SLEEP`, where it idles efficiently while awaiting a print command.

When the Actuator initiates a print job, it sends a CoAP `"START"` command to the Sensor, triggering the transition into `STATE_ACTIVE`. During active sampling, a 1-second timer triggers the mathematical simulation of the physical environment, generating multi-axis plate and extruder accelerations, voltage tension, and power consumption. The simulation uses bounded Gaussian distributions to mimic realistic data profiles. To test the ML model, the firmware incorporates a stochastic probability roll (`get_uniform_probability`) that occasionally forces a natural transition from `PRINTER_STATE_NORMAL` into `PRINTER_STATE_ERROR`, instantly altering the standard deviation and means of the generated telemetry to simulate a mechanical failure.

Because constrained radio resources must be managed carefully during active telemetry, data transmission is strictly paced by the Actuator using a "**Ping-Pong**" batching mechanism. The Sensor serializes its simulated data into SenML-compliant JSON strings and publishes them via MQTT to `<sensor_ip>/print/measurements`. Once exactly 5 samples (the `target_batch_size`) are published, the Sensor pauses its sampling timer and waits. It will only resume sampling if it receives a CoAP `"CONT"` command from the Actuator indicating a successful ML verdict, or it will revert to `STATE_SLEEP` if a `"STOP"` command is received.

To guarantee system resilience across the decoupled architecture, the firmware runs an independent `health_check_process` protothread. Every 60 seconds, it sends a CoAP ping to the Actuator’s `/health` endpoint. If the Actuator fails to respond three consecutive times, the Sensor assumes the Actuator has died or disconnected, safely breaks the pairing, and restarts its discovery phase.

Similar to the Actuator, a long button press (**5+ seconds**) from any active state forces a hard reset. This kills the parallel processes, stops active timers, transmits a direct CoAP `"OFF"` signal to decouple from the Actuator, and completely resets the device back to `STATE_OFF`.

<figure id="fig:SENSOR-LED-STATE" data-latex-placement="H">
<img src="doc/img/Sensor_Led.png" style="width:80.0%" />
<figcaption>LED for all the states</figcaption>
</figure>

To keep the main application loop clean, peripheral functionalities are isolated across dedicated compilation modules:
- `sensors_main.c`: Manages the global Contiki-NG processes, the primary state machine logic, the health-checking routine, mathematical environment simulation, JSON payload serialization, and the batching synchronization timers;
- `coap_module.c`: Orchestrates the UDP control plane, managing the iterative asynchronous discovery process across known IPs, receiving external pacing commands (`"START"`, `"CONT"`, `"STOP"`) from the paired Actuator, and transmitting unpair warnings;
- `mqtt_module.c`: Governs the connection state with the TCP MQTT broker, formatting client credentials, and pushing high-frequency QoS 1 telemetry publications during the active state.

### Machine Learning Model
The code related to the Machine Learning has been placed inside the subfolder *Eco3DPrint/ml*.

The **Machine Learning model** for the IoT device originates from the **Joanna-3D-Printing-Data dataset** [7](#ref7). Time Series were extracted from the dataset results by splitting the data in the same results file into multiple Time Series if there were at least two minutes between measurements. These were then divided into correct (successful) and erroneous (failure) Time Series.

Following the data extraction, the Python Notebook `ML_Model.ipynb` documents the Machine Learning pipeline:
- **Extrapolated Data:** The notebook loads 6 correct Time Series and 10 error Time Series, exploring statistical properties (such as mean, standard deviation, minimum, maximum and peak-to-peak) for features like the X, Y, and Z axes for both the plate and extrusion, alongside tension measurements. It calculates the global average sampling interval to confirm the  ≈ 5*m**s* frequency (approximately 196Hz);
- **Window Model for Fault Detection:** A sliding window approach is utilized for fault detection. This technique captures the temporal dynamics of the 3D Printer’s vibrations and power draw over short periods, allowing the edge device to process small batches of data and detect anomalies in real-time without exceeding hardware memory limits. *We use a 5-second window with the step size between consecutive windows as the parameter to optimize between 0, 1, 2, 3, and 4 seconds*;
- **Preprocessing:** For the Deep Neural Network, the raw sensor data is standardized using a `StandardScaler` to ensure all features contribute equally to the learning process. The notebook also employs `compute_class_weight` to handle class imbalances between the successful and failed print data.

To build the predictive models, the notebook evaluates traditional ensemble methods alongside deep learning. It implements **Random Forest** and **Extra Trees**, utilizing `GridSearchCV` and `GroupKFold` for rigorous hyperparameter tuning and cross-validation across the different Time Series groups. A **Deep Neural Network** is implemented using `TensorFlow` and `Keras`. The architecture incorporates regularizers to prevent overfitting and utilizes callbacks like `EarlyStopping` and `ReduceLROnPlateau` to dynamically optimize the learning rate and halt training when performance plateaus. Finally, the trained Neural Network is converted into a highly optimized C header file (`print_prediction.h`) using the emlearn library, enabling direct deployment on the constrained IoT edge device.

The best model among all Random Forests, Extra Trees and the Deep Neural Networks turns out to be one of the latter: **the Deep Neural Network with 1-second step**. This final model achieves high predictive Accuracy ( ≈ 85%) for fault detection. However, the test set was used more as a validation set as indicated in the professor’s Notebooks even if it is not technically the correct practice.

### Backend Cloud Application
#### Core Executable
The code related to the backend of the Cloud App has been placed inside the subfolder *Eco3DPrint/src/CloudApp/backend*. The core executable (`server.py`) acts as the entry point, spawning dedicated threads for each major subsystem to ensure non-blocking operations. A centralized graceful shutdown mechanism binds to *OS-level signals* (`SIGINT`) to safely terminate network event loops, disconnect database pools, and update physical node states to `OFFLINE` before exiting.

#### Database Structure
The system stores data in a relational MySQL database, `Eco3DPrintDB`, organized into three tables. The schema separates device metadata, print-job records, and high-frequency telemetry while preserving referential integrity through primary and foreign keys.

##### Node Table
| **Field**   | **Type**     | **Constraints**       |
|:------------|:-------------|:----------------------|
| ip          | VARCHAR(255) | PRIMARY KEY, NOT NULL |
| name        | VARCHAR(255) | NOT NULL              |
| type        | VARCHAR(255) | NOT NULL              |
| utilization | VARCHAR(255) | NOT NULL              |
| sensor_ip   | VARCHAR(255) | NOT NULL              |
| status      | VARCHAR(255) | NOT NULL              |

##### Print Table
| **Field**       | **Type**     | **Constraints**                       |
|:----------------|:-------------|:--------------------------------------|
| id              | INT          | PRIMARY KEY, AUTO_INCREMENT, NOT NULL |
| ip              | VARCHAR(255) | NOT NULL, FK → Node(ip)               |
| stl_name        | VARCHAR(255) | NOT NULL                              |
| status          | VARCHAR(255) | NOT NULL                              |
| activation_time | TIMESTAMP    | NOT NULL, DEFAULT CURRENT_TIMESTAMP   |
| energy          | DOUBLE       | NOT NULL                              |

##### Measurement Table
| **Field** | **Type** | **Constraints** |
|:---|:---|:---|
| timestamp | TIMESTAMP | PRIMARY KEY, NOT NULL, DEFAULT CURRENT_TIMESTAMP |
| print_id | INT | PRIMARY KEY, NOT NULL, FK → Print(id) |
| ip | VARCHAR(255) | PRIMARY KEY, NOT NULL, FK → Print(ip) |
| X_Axis_Plate | DOUBLE |  |
| Y_Axis_Plate | DOUBLE |  |
| Z_Axis_Plate | DOUBLE |  |
| X_Axis_Extrusion | DOUBLE |  |
| Y_Axis_Extrusion | DOUBLE |  |
| Z_Axis_Extrusion | DOUBLE |  |
| Tension | DOUBLE |  |
| Power | DOUBLE |  |

The `Node` table stores printer metadata and status (*OFFLINE*, *ONLINE*, *PRINTING*). The `Print` table logs each print job and links it to its source node, with status that represents the outcome of the print.. The `Measurement` table records telemetry samples for each print job using a composite primary key `(timestamp, print_id, ip)`.

#### Data Persistence and Connection Pooling
Data storage is handled by a custom Database Access Layer (`Database.py`) connected to MySQL. It uses connection pooling to ensure thread-safe operations and prevent bottlenecks during concurrent access by multiple subsystems.

#### CoAP Control Plane
Low-frequency control events and system state transitions are handled by an IPv6-enabled CoAP Server (`CoAPServer.py`). The server exposes several distinct resource endpoints. The **/registration** endpoint (`NodeRegistration.py`) processes initial handshakes from connecting hardware, storing their metadata. The **/signal/off** endpoint (`OFFSignal.py`) receives hardware interrupts, immediately updating the system to reflect a disconnected state. Finally, the **/print/finished** endpoint (`PrintFinished.py`) processes end-of-print notifications, logging total energy consumption and updating the job queue.

#### MQTT Telemetry Plane
High-frequency sensory data is managed by a dedicated background MQTT subscriber (`MQTTHandler.py`). This component continuously ingests telemetry streams published on the `+/print/measurements` topic, utilizing the MQTT `+` wildcard to dynamically capture data from any active Sensor IPv6 address.

Upon receiving a message, the handler extracts the Sensor's IP directly from the topic string and parses the incoming SenML-compliant JSON array. Crucially, before storage, it executes a relational database query to identify the active `print_id` linked to the specific Actuator currently paired with that Sensor IP, and securely stores the exact simulated physical metrics in the `Measurement` table using parameterized `INSERT IGNORE` queries.

#### Node Monitoring
To maintain a real-time ledger of network health, an autonomous watchdog module (`NodeMonitor.py`) continuously tracks active devices. It operates on a scheduled interval, dispatching CoAP **/health** ping requests to nodes currently marked as active or printing. If a node fails to respond after a predefined number of retries, the monitor dynamically transitions its state to offline within the database and instantly broadcasts this state change across the system via an internal event emitter.

#### Job Orchestration and Binary Transfer
The queueing and dispatching of 3D models is controlled by the print orchestration module (`PrintManager.py`). When a node becomes available, this component retrieves the next pending job. To overcome the memory and transmission constraints of the edge devices, it utilizes **CoAP block-wise transfer** to partition large binary **STL files** into sequential chunks, ensuring reliable, acknowledged delivery without overloading the target node’s memory buffers.

#### User Interface Bridging
An asynchronous full-duplex connection, handled by a dedicated module (`Socket.py`), links the cloud layer to the UI. Running on its own asyncio loop, it streams live node updates to the dashboard and processes analytical requests, returning aggregated daily, weekly, and monthly energy reports.

### Frontend User Application
#### GUI
The code related to the frontend of the Cloud App has been placed inside the subfolder *Eco3DPrint/src/CloudApp/frontend*. The application is a desktop **Graphical User Interface** built with Python tkinter that connects operators to backend cloud services for real-time monitoring and energy analytics. To prevent orphaned network tasks and ensure system stability upon termination, the core entry point (`app.py`) securely binds to **OS-level signals**, such as `SIGINT` (*CTRL+C*) and window manager events.

It ensures stability by handling OS signals and performing a graceful shutdown, stopping polling loops, closing the GUI, and terminating WebSocket threads.

#### State Routing
To maintain a highly responsive interface without GUI freezing, the system decouples network I/O from the main rendering thread. It spawns a dedicated background daemon thread running an `asyncio` event loop that maintains a persistent, **full-duplex Web Socket** connection to the backend. Incoming JSON telemetry and status updates are buffered into a thread-safe queue. The primary tkinter loop continuously polls this queue, parses the payloads, and utilizes an intelligent routing mechanism to distribute the data to the correct screen module based on the message’s defined type.

#### Printer Management Dashboard
The primary operational view (`Printers.py`) dynamically generates a responsive grid of interactive cards representing the physical 3D Printer nodes. To provide immediate situational awareness, the module applies dynamic color-filtering overlays to the printer icons, visually distinguishing their current operational states (green for *ONLINE*, red for *OFFLINE*, yellow for *PRINTING*). Operators can interact with these nodes through modal dialogs, which display detailed node metadata and provide the control interfaces necessary to query the backend for available STL models and dispatch them to the edge devices.

#### Analytical Reporting Modules
Historical telemetry and operational metrics are encapsulated within three distinct reporting modules (`DailyReport.py`, `WeeklyReport.py`, and `MonthlyReport.py`).
- **Data Request and Conversion:** These modules actively push commands to the backend to request aggregated datasets and autonomously perform unit conversions upon receipt, translating raw energy measurements from Joules into kilowatt-hours (kWh) for human readability;
- **Visualization:** Metrics are rendered using high-level summary cards and categorized into scrollable tabular formats utilizing ttk.Treeview widgets, separating successfully utilized energy from energy wasted during failed prints;
- **Temporal Formatting:** To guarantee structural consistency in data visualization, the weekly report implements a zero-padding algorithm that populates missing operational days with baseline metrics. Conversely, the monthly module aggregates the incoming monthly arrays to calculate and display ongoing Year-To-Date (YTD) performance totals.

## Use Case
### Overall Objective
The main goal of this application is to monitor energy usage to encourage smart decisions within 3D printer companies. This prevents both energy waste and the waste of materials used in printing. This would save money and resources in the long run, optimizing prints and reducing energy bills.

### Energy Consumption Monitoring and Sustainability Analytics
As manufacturing facilities scale their 3D printer fleets, tracking and optimizing energy consumption becomes a critical operational requirement. ***Eco3DPrint*** acts as an advanced telemetry pipeline, serializing real-time power measurements into SenML-compliant JSON payloads and transmitting them to a central database. The system’s cloud backend aggregates this data to perform granular cost accounting and sustainability analysis. Through the frontend interface, operators can generate comprehensive daily, weekly, and monthly reports. Crucially, the system dichotomizes power consumption into "well-used" energy (attributed to successfully completed prints) and "wasted" energy (attributed to aborted or failed prints). This visibility enables facility managers to identify inefficient hardware, optimize print parameters to reduce failure rates, and accurately calculate the true energy overhead of their additive manufacturing processes.

### Predictive Maintenance and Automated Fault Detection
In traditional 3D Printing Environments, mechanical failures such as layer shifting, print bed detachment, or nozzle clogging often go unnoticed until the print cycle is manually inspected. This results in significant material waste and prolonged equipment downtime. ***Eco3DPrint*** addresses this through autonomous edge intelligence. By continuously sampling multi-axis vibration and power draw via attached sensor nodes (e.g., nRF52840 dongles equipped with accelerometers and power monitors), the system captures the high-frequency physical state of the machine. An embedded Machine Learning model analyzes these localized time-series windows to detect anomalies in real time. Upon identifying a critical failure signature for consecutive cycles, the edge node autonomously aborts the printing process. This rapid, localized decision-making minimizes material waste and prevents potential hardware damage without relying on constant cloud connectivity or human supervision.

### Remote Fleet Orchestration and Job Dispatching
Managing a distributed array of additive manufacturing nodes requires a centralized, responsive command interface. ***Eco3DPrint*** provides a full-duplex, asynchronous control plane that allows human operators to monitor and manipulate the entire fleet from a single desktop application. Through the graphical dashboard, users receive immediate, color-coded situational awareness regarding the availability and operational status of each node. Furthermore, the system streamlines the production workflow by enabling remote job dispatching. Operators can seamlessly query the backend for available STL models and queue them for specific printers. The backend orchestrates the reliable delivery of these large binary assets using CoAP block-wise transfer, overcoming the memory constraints of the physical edge nodes and ensuring that production can be scaled efficiently without physical intervention at each machine.

### Application Working (on Cooja)
<figure id="fig:Screenshot-1" data-latex-placement="H">
<img src="doc/img/Screenshot_1.png" style="width:75%" />
<figcaption>Starting setup.sh</figcaption>
</figure>

<figure id="fig:Screenshot-2" data-latex-placement="H">
<img src="doc/img/Screenshot_2.png" style="width:75%" />
<figcaption>Starting Cooja</figcaption>
</figure>

<figure id="fig:Screenshot-3" data-latex-placement="H">
<img src="doc/img/Screenshot_3.png" style="width:75%" />
<figcaption>Cooja Example with 2 Printers and 2 Sensors</figcaption>
</figure>

<figure id="fig:Screenshot-4" data-latex-placement="H">
<img src="doc/img/Screenshot_4.png" style="width:75%" />
<figcaption>Start Cooja, Mosquitto, Server and User App</figcaption>
</figure>

<figure id="fig:Screenshot-5" data-latex-placement="H">
<img src="doc/img/Screenshot_5.png" style="width:75%" />
<figcaption>User App</figcaption>
</figure>

<figure id="fig:Screenshot-6" data-latex-placement="H">
<img src="doc/img/Screenshot_6.png" style="width:75%" />
<figcaption>Click button and start the Mote</figcaption>
</figure>

<figure id="fig:Screenshot-7" data-latex-placement="H">
<img src="doc/img/Screenshot_7.png" style="width:75%" />
<figcaption>Printer 1 Online, Printer 2 Online, and Printer 3 Offline</figcaption>
</figure>

<figure id="fig:Screenshot-8" data-latex-placement="H">
<img src="doc/img/Screenshot_8.png" style="width:75%" />
<figcaption>Send STL 1</figcaption>
</figure>

<figure id="fig:Screenshot-9" data-latex-placement="H">
<img src="doc/img/Screenshot_9.png" style="width:75%" />
<figcaption>Send STL 2</figcaption>
</figure>

<figure id="fig:Screenshot-10" data-latex-placement="H">
<img src="doc/img/Screenshot_10.png" style="width:75%" />
<figcaption>Send STL 3</figcaption>
</figure>

<figure id="fig:Screenshot-11" data-latex-placement="H">
<img src="doc/img/Screenshot_11.png" style="width:75%" />
<figcaption>Printer 1 Printing, Printer 2 Online, and Printer 3 Offline</figcaption>
</figure>

<figure id="fig:Screenshot-12" data-latex-placement="H">
<img src="doc/img/Screenshot_12.png" style="width:75%" />
<figcaption>Failed Print</figcaption>
</figure>

<figure id="fig:Screenshot-13" data-latex-placement="H">
<img src="doc/img/Screenshot_13.png" style="width:75%" />
<figcaption>Successful Print</figcaption>
</figure>

<figure id="fig:Screenshot-14" data-latex-placement="H">
<img src="doc/img/Screenshot_14.png" style="width:75%" />
<figcaption>Daily Report</figcaption>
</figure>

<figure id="fig:Screenshot-15" data-latex-placement="H">
<img src="doc/img/Screenshot_15.png" style="width:75%" />
<figcaption>Weekly Report</figcaption>
</figure>

<figure id="fig:Screenshot-16" data-latex-placement="H">
<img src="doc/img/Screenshot_16.png" style="width:75%" />
<figcaption>Monthly Report</figcaption>
</figure>

## Conclusion
### Ending the Report
In conclusion, the ***<u>Eco3DPrint</u>*** architecture successfully realizes a comprehensive Industrial Internet of Things (IIoT) ecosystem by seamlessly integrating the four essential functional layers of modern connected systems. Starting at the edge, constrained physical devices and sensors autonomously collect high-frequency environmental data and execute local Machine Learning inferences to proactively detect mechanical anomalies. This edge intelligence is linked via robust connectivity protocols, utilizing CoAP for lightweight control and MQTT for continuous telemetryl, ensuring reliable data transmission even under network constraints.

At the higher levels, the multi-threaded cloud processing layer efficiently orchestrates concurrent data streams, persistent storage, and fleet management. Finally, the asynchronous user interface transforms raw telemetry into actionable, real-time analytics and energy consumption reports. By bridging predictive maintenance with granular energy tracking, this system provides a highly scalable, responsive, and resource-efficient solution for remote 3D Printer fleet management.

### Improvements for Future Works
Several improvements can be introduced to enhance the system’s usage:
- **Advanced Diagnostic AI:** The current neural network performs binary classification, identifying only normal operation versus failure. Future versions could enable multi-class classification by training on specific failure signatures (e.g., layer shifting, bed detachment, nozzle clogs, filament runout), allowing more precise diagnostics alongside fault alerts;
- **Machine Learning with Camera:** Combine the existing vibration and tension based model with another ML Model on a real-time videocamera model to detect print issues and decide when to stop, improving fault detection and saving resources;
- **End-to-End Security Implementation:** As an Industrial IoT system managing proprietary STL files and operational data, securing communications is critical. Future development should implement TLS for MQTT telemetry and DTLS for the CoAP control plane, ensuring all sensor data and file transfers are encrypted against interception and tampering;
- **Cloud Scalability and Time-Series Optimization:** The current Python backend and MySQL database work very well for small to medium fleets, but scaling to thousands of nodes requires architectural changes. A containerized microservices setup (Kubernetes) would enable independent scaling of components, while replacing MySQL with a time-series database like InfluxDB would improve handling of high-frequency sensor data;
- **Direct Hardware Integration:** Interfacing the physical sensors directly to the 3D printer's embedded controller (e.g., via I2C or SPI buses) would eliminate the reliance on local CoAP synchronization between decoupled nodes. This hardwired approach mitigates wireless latency and protocol overhead, facilitating faster, closed-loop telemetry acquisition and immediate actuator response.
- **Utilization Parameter:** Currently the Utilization parameter in the MySQL Node table is not used and has been put aside for future use;
- **Improving STL Transmission:** Some STLs are very large and are not convenient to send via CoAP. In the future, a more suitable protocol will be needed for sending STLs.

## References & Bibliography
1. <a id="ref1"></a> **Majhi, Abhilash; Kumar, Asit; Mohanty, Sanjeeb**  
   *A Comprehensive Review on Internet of Things Applications in Power Systems* (2024). IEEE.  
   [Link](https://www.researchgate.net/publication/383289893_A_Comprehensive_Review_on_Internet_of_Things_Applications_in_Power_Systems)

2. <a id="ref2"></a> **Contiki-NG Wiki** (2026).  
   [Link](https://github.com/contiki-ng/contiki-ng/wiki)

3. <a id="ref3"></a> **CoAPthon**  
   [Link](https://github.com/Tanganelli/CoAPthon)

4. <a id="ref4"></a> **Paho**  
   [Link](https://pypi.org/project/paho-mqtt/)

5. <a id="ref5"></a> **Tkinter**  
   [Link](https://docs.python.org/3/library/tkinter.html)

6. <a id="ref6"></a> **SenML - A Lightweight Format for Representing Sensor Data**  
   [Link](https://www.rfc-editor.org/rfc/rfc8428.html)

7. <a id="ref7"></a> **Szydlo, Tomasz; Sendorek, Joanna; Windak, Mateusz; Brzoza-Woch, Robert**  
   *Dataset for Anomalies Detection in 3D Printing* (2021). In *Computational Science -- ICCS 2021*. Springer International Publishing, Cham.  
   [Link](https://github.com/joanna-/3D-Printing-Data)venting failed prints, maintaining both economic and environmental viability.
