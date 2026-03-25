-- Database initialization for Eco3DPrintDB
CREATE DATABASE IF NOT EXISTS Eco3DPrintDB;
USE Eco3DPrintDB;

-- Create user if it does not exist
CREATE USER IF NOT EXISTS 'iotuser'@'localhost' IDENTIFIED BY 'iotpassword';
GRANT ALL PRIVILEGES ON Eco3DPrintDB.* TO 'iotuser'@'localhost';
FLUSH PRIVILEGES;

/* 
	Node table definition
	Stores information about each 3D printer node on the network
*/
CREATE TABLE IF NOT EXISTS Node (
    ip VARCHAR(255) NOT NULL,
    name VARCHAR(255) NOT NULL,
    type VARCHAR(255) NOT NULL,
    utilization VARCHAR(255) NOT NULL,
    status VARCHAR(255) NOT NULL,
    PRIMARY KEY (ip)
) ENGINE=InnoDB;

/* 
	Print table definition
	Stores individual print jobs associated with a specific Node
*/
CREATE TABLE IF NOT EXISTS `Print` (
    id INT NOT NULL AUTO_INCREMENT,
    ip VARCHAR(255) NOT NULL,
    stl_name VARCHAR(255) NOT NULL,
    status VARCHAR(255) NOT NULL,
    activation_time TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    energy DOUBLE NOT NULL,
    PRIMARY KEY (id),
    -- Index required for the foreign key reference in the Measurement table
    INDEX idx_ip (ip), 
    CONSTRAINT fk_node_ip FOREIGN KEY (ip) REFERENCES Node(ip) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB;

/* 
	Measurement table definition
	Stores telemetry data, structural axes, and power usage during a print
*/
CREATE TABLE IF NOT EXISTS Measurement (
    timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    print_id INT NOT NULL,
    ip VARCHAR(255) NOT NULL,
    x_axis_plate DOUBLE,
    y_axis_plate DOUBLE,
    z_axis_plate DOUBLE,
    x_axis_extrusion DOUBLE,
    y_axis_extrusion DOUBLE,
    z_axis_extrusion DOUBLE,
    tension DOUBLE,
    power DOUBLE,
    PRIMARY KEY (timestamp, print_id, ip),
    CONSTRAINT fk_print_id FOREIGN KEY (print_id) REFERENCES `Print`(id) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB;