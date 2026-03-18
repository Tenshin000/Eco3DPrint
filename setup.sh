#---------------------------------------------#
#                 CONFIGURATION               #
#---------------------------------------------#
DB_USER="iotuser"
DB_PASS="iotpassword"
DB_NAME="Eco3DPrintDB"
SRC_DIR="src"

#---------------------------------------------#
#                  FUNCTIONS                  #
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

build_border_router(){
    local mode=$1
    if [ "$mode" != "cooja" ]; then
        gnome-terminal -- bash -c "cd ./'${SRC_DIR}'/Gateway; make TARGET=nrf52840 BOARD=dongle PORT=/dev/ttyACM0 connect-router; exec bash"
        echo "Border router connecting to dongle"
    else
        gnome-terminal -- bash -c "cd ./'${SRC_DIR}'/Gateway; make TARGET=cooja connect-router-cooja; exec bash"
        echo "Border router connecting to Cooja"
    fi
}

function start_server(){
    gnome-terminal -- bash -c 'cd ./'${SRC_DIR}'/CloudApp; python server.py; exec bash'
}

function start_app(){
    gnome-terminal -- bash -c 'cd ./'${SRC_DIR}'/CloudApp; python app.py; exec bash'
}

function start_mosquito(){
    sudo pkill mosquitto 2>/dev/null
    gnome-terminal -- bash -c "cd ./${SRC_DIR}/CloudApp; mosquitto -c local_broker.conf -v; exec bash"
}

#---------------------------------------------#
#                    CLI                      #
#---------------------------------------------#
case $1 in
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
    -br)
        build_border_router $2
        ;;
    -sm)
    	start_mosquito
    	;;
    -sim)
        echo "Starting Cooja..."
        launch_cooja
        echo "Press any key to start the border-router..."
        read -n 1 -s
        build_border_router "cooja"
        echo "Press any key to start the mosquito server..."
        read -n 1 -s
        start_mosquito
        echo "Press any key to start the Server and the App..."
        read -n 1 -s
        start_server
        start_app
        ;;
        *)
        echo "===================================================================="
        echo "=                          Eco3DPrint CLI                          ="
        echo "===================================================================="
        echo
        echo "USAGE:"
        echo "  $0 <command> [options]"
        echo
        echo "--------------------------------------------------------------------"
        echo "                        SIMULATION COMMANDS"
        echo "--------------------------------------------------------------------"
        echo "  -cooja"
        echo "      Launch the Cooja network simulator."
        echo
        echo "  -sim"
        echo "      Start the full simulation workflow:"
        echo "        1) Launch Cooja"
        echo "        2) Start the border router (Cooja mode)"
        echo "        3) Start Cloud server and application"
        echo
        echo "  -br <target>"
        echo "      Start the RPL border router."
        echo "      target:"
        echo "        cooja    -> connect router to Cooja simulation"
        echo "        hardware -> connect router to nRF52840 dongle"
        echo
        echo "--------------------------------------------------------------------"
        echo "                          DATABASE MANAGEMENT"
        echo "--------------------------------------------------------------------"
        echo "  -create-db"
        echo "      Install MySQL if missing and initialize the database."
        echo
        echo "  -sql \"<query>\""
        echo "      Execute a custom SQL query on ${DB_NAME}."
        echo
        echo "      Example:"
        echo "      $0 -sql \"SELECT * FROM Node;\""
        echo
        echo "--------------------------------------------------------------------"
        echo "                          TABLE VISUALIZATION"
        echo "--------------------------------------------------------------------"
        echo "  -nodes"
        echo "      Display the Node table."
        echo
        echo "  -prints"
        echo "      Display the Print table."
        echo
        echo "  -measurements"
        echo "      Display the Measurement table."
        echo
        echo "  -table <table_name>"
        echo "      Display the contents of any table."
        echo
        echo "      Examples:"
        echo "      $0 -table Node"
        echo "      $0 -table Print"
        echo "      $0 -table Measurement"
        echo
        echo "===================================================================="
        exit 1
        ;;
esac
