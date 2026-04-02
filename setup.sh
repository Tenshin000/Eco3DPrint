#!/bin/bash

#---------------------------------------------#
#                 CONFIGURATION               #
#---------------------------------------------#
DB_USER="iotuser"
DB_PASS="iotpassword"
DB_NAME="Eco3DPrintDB"
SRC_DIR="src"

#---------------------------------------------#
#                 FUNCTIONS                   #
#---------------------------------------------#
check_mysql_install(){
    if ! command -v mysql >/dev/null 2>&1; then
        echo "MySQL not detected. Installing..."
        sudo apt update
        sudo apt install -y mysql-server
        if [ $? -ne 0 ]; then
            echo "Failed to install MySQL."
            exit 1
        fi
    else
        echo "MySQL is installed."
    fi

    if ! systemctl is-active --quiet mysql; then
        echo "Starting MySQL service..."
        sudo systemctl start mysql
    fi
}

ensure_db_user(){
    local exists
    exists=$(sudo mysql -sse "SELECT EXISTS(SELECT 1 FROM mysql.user WHERE user='${DB_USER}');")
    if [ "$exists" = "0" ]; then
        echo "Creating MySQL user '${DB_USER}'..."
        sudo mysql -e "CREATE USER '${DB_USER}'@'localhost' IDENTIFIED BY '${DB_PASS}';"
        sudo mysql -e "GRANT ALL PRIVILEGES ON *.* TO '${DB_USER}'@'localhost' WITH GRANT OPTION;"
        sudo mysql -e "FLUSH PRIVILEGES;"
    else
        echo "User '${DB_USER}' already exists."
    fi
}

setup_database(){
    echo "Removing old database..."
    sudo mysql -e "DROP DATABASE IF EXISTS ${DB_NAME};"
    
    echo "Creating new database..."
    sudo mysql -e "CREATE DATABASE IF NOT EXISTS ${DB_NAME};"
    
    echo "Assigning privileges..."
    sudo mysql -e "GRANT ALL PRIVILEGES ON ${DB_NAME}.* TO '${DB_USER}'@'localhost';"
    sudo mysql -e "FLUSH PRIVILEGES;"
}

create_tables(){
    sudo mysql -e "USE ${DB_NAME};
    CREATE TABLE IF NOT EXISTS Node (
        ip VARCHAR(255) NOT NULL,
        name VARCHAR(255) NOT NULL,
        type VARCHAR(255) NOT NULL,
        utilization VARCHAR(255) NOT NULL,
        status VARCHAR(255) NOT NULL,
        PRIMARY KEY (ip)
    );"
    echo -e "\t- Node table ready"

    sudo mysql -e "USE ${DB_NAME};
    CREATE TABLE IF NOT EXISTS \`Print\` (
        id INT NOT NULL AUTO_INCREMENT,
        ip VARCHAR(255) NOT NULL,
        stl_name VARCHAR(255) NOT NULL,
        status VARCHAR(255) NOT NULL,
        activation_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
        energy DOUBLE NOT NULL,
        PRIMARY KEY (id),
        CONSTRAINT fk_node_ip FOREIGN KEY (ip) REFERENCES Node(ip) ON DELETE CASCADE ON UPDATE CASCADE
    );"
    echo -e "\t- Print table ready"

    sudo mysql -e "USE ${DB_NAME};
    CREATE TABLE IF NOT EXISTS Measurement (
        timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
        print_id INT NOT NULL,
        ip VARCHAR(255) NOT NULL,
        X_Axis_Plate DOUBLE,
        Y_Axis_Plate DOUBLE,
        Z_Axis_Plate DOUBLE,
        X_Axis_Extrusion DOUBLE,
        Y_Axis_Extrusion DOUBLE,
        Z_Axis_Extrusion DOUBLE,
        Tension DOUBLE,
        Power DOUBLE,
        PRIMARY KEY (timestamp, print_id, ip),
        CONSTRAINT fk_print_id FOREIGN KEY (print_id) REFERENCES \`Print\`(id) ON DELETE CASCADE ON UPDATE CASCADE,
        CONSTRAINT fk_print_ip FOREIGN KEY (ip) REFERENCES \`Print\`(ip) ON DELETE CASCADE ON UPDATE CASCADE
    );"
    echo -e "\t- Measurement table ready"
}

create_db() {
    check_mysql_install
    ensure_db_user
    setup_database
    create_tables
    echo "Database initialized successfully!"
}

install_dependencies(){
    echo "Installing Tkinter..."
    sudo apt update
    sudo apt install -y python3-tk python3-pil.imagetk

    if [ -f "requirements.txt" ]; then
        echo "Installing Python dependencies from requirements.txt..."
        pip install -r requirements.txt
    else
        echo "Warning: requirements.txt not found."
    fi
}

full_setup(){
    echo "Starting full system setup..."
    create_db
    install_dependencies
    echo "Setup completed successfully!"
}

execute_sql(){
    mysql -u$DB_USER -p$DB_PASS -D$DB_NAME -e "$1"
}

show_table(){
    local table_name=$1
    echo "Displaying contents of table '${table_name}' from database '${DB_NAME}'..."
    execute_sql "SELECT * FROM ${table_name};"
}

show_nodes(){
    show_table "Node"
}

show_prints(){
    show_table "Print"
}

show_measurements(){
    show_table "Measurement"
}

launch_cooja(){
    gnome-terminal -- bash -c "cd; cd contiki-ng/tools/cooja; ./gradlew run; exec bash"
}

flash_border_router(){
    local port=${1:-/dev/ttyACM0}
    echo "Flashing Border Router on ${port}..."
    gnome-terminal --wait -- bash -c "cd ./${SRC_DIR}/Gateway && make TARGET=nrf52840 BOARD=dongle PORT=${port} border-router.dfu-upload; echo 'Flash complete. Press any key to close.'; read -n 1 -s"
}

flash_device(){
    local port=${1:-/dev/ttyACM1}
    echo "Flashing Device/Node on ${port}..."
    gnome-terminal --wait -- bash -c "cd ./${SRC_DIR}/Printer3D && make TARGET=nrf52840 BOARD=dongle PORT=${port} device.dfu-upload; echo 'Flash complete. Press any key to close.'; read -n 1 -s"
}

login_node(){
    local port=${1:-/dev/ttyACM1}
    echo "Opening serial monitor for Device/Node on ${port}..."
    gnome-terminal -- bash -c "cd ./${SRC_DIR}/Printer3D && make login TARGET=nrf52840 BOARD=dongle PORT=${port}; exec bash"
}

build_border_router(){
    local mode=$1
    local port=${2:-/dev/ttyACM0}
    if [ "$mode" != "cooja" ]; then
        gnome-terminal -- bash -c "cd ./'${SRC_DIR}'/Gateway; make TARGET=nrf52840 BOARD=dongle PORT=${port} connect-router; exec bash"
        echo "Border router connecting to dongle on ${port}"
    else
        gnome-terminal -- bash -c "cd ./'${SRC_DIR}'/Gateway; make TARGET=cooja connect-router-cooja; exec bash"
        echo "Border router connecting to Cooja"
    fi
}

start_server(){
    gnome-terminal -- bash -c "cd ./${SRC_DIR}/CloudApp; python3 server.py; exec bash"
}

start_app(){
    gnome-terminal -- bash -c "cd ./${SRC_DIR}/CloudApp; python3 app.py; exec bash"
}

start_mosquitto(){
    sudo pkill mosquitto 2>/dev/null
    gnome-terminal -- bash -c "cd ./${SRC_DIR}/CloudApp; mosquitto -c local_broker.conf -v; exec bash"
}

start_hardware_sim(){
    echo "===================================================================="
    echo "=                Starting Hardware Simulation Workflow             ="
    echo "===================================================================="
    echo
    echo "Do you want to flash the Border Router Dongle on /dev/ttyACM0 first? (y/n)"
    read -n 1 -s answer_br
    echo
    if [[ "$answer_br" =~ ^[Yy]$ ]]; then
        echo "-> Please ensure the Border Router dongle is pulsing RED."
        flash_border_router "/dev/ttyACM0"
    fi

    echo "Do you want to flash the Device/Node Dongle on /dev/ttyACM1 first? (y/n)"
    read -n 1 -s answer_node
    echo
    if [[ "$answer_node" =~ ^[Yy]$ ]]; then
        echo "-> Please ensure the Device/Node dongle is pulsing RED."
        flash_device "/dev/ttyACM1"
    fi

    echo "--------------------------------------------------------------------"
    echo "Press any key to connect the border-router to the Gateway Dongle (/dev/ttyACM0)..."
    read -n 1 -s
    build_border_router "hardware" "/dev/ttyACM0"
    
    echo "Press any key to open the serial monitor (login) for the Node (/dev/ttyACM1)..."
    read -n 1 -s
    login_node "/dev/ttyACM1"
    
    echo "Press any key to start the Mosquitto MQTT server..."
    read -n 1 -s
    start_mosquitto
    
    echo "Press any key to start the Cloud Server and the App..."
    read -n 1 -s
    start_server
    start_app
    
    echo "Hardware simulation environment successfully launched!"
    echo "===================================================================="
}

#---------------------------------------------#
#                  CLI                        #
#---------------------------------------------#
case $1 in
    -setup)
        full_setup
        ;;
    -cooja)
        launch_cooja
        ;;
    -create-db)
        create_db
        ;;
    -sql)
        execute_sql "$2"
        ;;
    -nodes)
        show_nodes
        ;;
    -prints)
        show_prints
        ;;
    -measurements)
        show_measurements
        ;;
     -table)
        show_table "$2"
        ;;
    -flash-br)
        flash_border_router "$2"
        ;;
    -flash-node)
        flash_device "$2"
        ;;
    -login-node)
        login_node "$2"
        ;;
    -br)
        build_border_router "$2" "$3"
        ;;
    -mosquitto)
        start_mosquitto
        ;;
    -sim | -simulation)
        echo "Starting Cooja..."
        launch_cooja
        echo "Press any key to start the border-router..."
        read -n 1 -s
        build_border_router "cooja"
        echo "Press any key to start the mosquitto server..."
        read -n 1 -s
        start_mosquitto
        echo "Press any key to start the Server and the App..."
        read -n 1 -s
        start_server
        start_app
        ;;
    -dongle)
        start_hardware_sim
        ;;
    *)
    echo "===================================================================="
    echo "=                      Eco3DPrint CLI                              ="
    echo "===================================================================="
    echo
    echo "USAGE:"
    echo "  $0 <command> [options]"
    echo
    echo "--------------------------------------------------------------------"
    echo "                        GENERAL COMMANDS"
    echo "--------------------------------------------------------------------"
    echo "  -setup"
    echo "      Full environment setup: Database, Tkinter, and Pip requirements."
    echo
    echo "--------------------------------------------------------------------"
    echo "                        HARDWARE COMMANDS"
    echo "--------------------------------------------------------------------"
    echo "  -dongle"
    echo "      Start the full hardware workflow (flashing + server step-by-step)."
    echo
    echo "  -flash-br [port]"
    echo "      Flash the Border Router code into the nRF52840 Dongle (default: /dev/ttyACM0)."
    echo
    echo "  -flash-node [port]"
    echo "      Flash the Device (Node) code into the nRF52840 Dongle (default: /dev/ttyACM1)."
    echo
    echo "  -login-node [port]"
    echo "      Open the serial monitor (login) for the Node (default: /dev/ttyACM1)."
    echo
    echo "--------------------------------------------------------------------"
    echo "                        SIMULATION COMMANDS"
    echo "--------------------------------------------------------------------"
    echo "  -cooja"
    echo "      Launch the Cooja network simulator."
    echo
    echo "  -sim | -simulation"
    echo "      Start the full simulation workflow (step-by-step)."
    echo
    echo "  -br <target> [port]"
    echo "      Start the RPL border router (cooja | hardware) [optional: port]."
    echo
    echo "  -mosquitto"
    echo "      Start the local Mosquitto MQTT broker."
    echo
    echo "--------------------------------------------------------------------"
    echo "                        DATABASE MANAGEMENT"
    echo "--------------------------------------------------------------------"
    echo "  -create-db"
    echo "      Install MySQL (if missing) and initialize the database."
    echo
    echo "  -sql \"<query>\""
    echo "      Execute a custom SQL query."
    echo
    echo "===================================================================="
    exit 1
    ;;
esac
