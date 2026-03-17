import os
import asyncio
import json
import websockets
from configparser import ConfigParser

from backend.PrintManager import PrintManager 
from backend.Database import Database
from utility.Log import Log

class WebSocketManager:
    """
    Manages WebSocket connections used to push real-time updates
    from the backend to connected frontend clients, and handles
    incoming requests from the frontend.
    """
    def __init__(self, get_state_func, print_manager=None, database=None):
        """
        Initialize the WebSocket manager.

        :param get_state_func: Function that returns the current system state.
        :param print_manager: Reference to the PrintManager component.
        :param database: Database instance for querying metrics. If None, it initializes a new one.
        """
        # Set of currently connected WebSocket clients
        self.clients = set()
        # Event loop used by the WebSocket server
        self.loop = None
        # Function used to retrieve the latest node state
        self.get_state_func = get_state_func
        
        # Logger for WebSocket server events
        self._logger = Log(
            logger_name="socket_logger",
            module_name="SOCKET_SERVER"
        ).get_logger()

        # Initialize or assign the Database instance
        if database is None:
            config = ConfigParser()
            config.read('./backend/config.ini')
            self._db = Database(
                host=config.get('mysql', 'host'),
                user=config.get('mysql', 'user'),
                password=config.get('mysql', 'password'),
                database=config.get('mysql', 'database')
            )
            if not self._db.connect():
                self._logger.error("Failed to connect to the database within WebSocketManager.")
        else:
            self._db = database

        # Initialize or assign the PrintManager instance
        if print_manager is None:
            # We pass the db instance to avoid multiple connection pools if not necessary
            self.print_manager = PrintManager(database=self._db)
        else:
            self.print_manager = print_manager

    async def handler(self, websocket, path=None):
        """
        Handle a new WebSocket client connection.

        When a client connects:
        1. It is added to the active client set.
        2. The current system state is immediately sent.
        3. The server listens for specific actions requested by the frontend.
        """
        # Register client connection
        self.clients.add(websocket)

        try:
            # Send the current system state immediately after connection
            await websocket.send(json.dumps(self.get_state_func()))

            # Keep connection alive while the client remains connected
            async for message in websocket:
                try:
                    data = json.loads(message)
                    action = data.get("action") # FIXED: Explicitly extract action from data
                    
                    # Check if the frontend is requesting the list of STL files
                    if action == "get_stls":
                        self._logger.info("Received request for STL list from frontend.")
                        stl_dir = os.path.join(os.path.dirname(__file__), 'stl')
                        files = []
                        if os.path.exists(stl_dir):
                            files = [f for f in os.listdir(stl_dir) if f.endswith('.stl')]
                        else:
                            self._logger.warning(f"STL directory not found at {stl_dir}")
                        
                        response = {"type": "stl_list", "files": files}
                        await websocket.send(json.dumps(response))
                    
                    # Check if the frontend is commanding an STL upload to a node
                    elif action == "send_stl":
                        target_ip = data.get("ip")
                        filename = data.get("file")
                        
                        if target_ip and filename and self.print_manager:
                            self._logger.info(f"Frontend requested STL transfer. Delegating to PrintManager.")
                            # Pass the request to the PrintManager
                            self.print_manager.add_print_job(target_ip, filename)
                        else:
                            self._logger.warning("Received invalid 'send_stl' command or PrintManager missing.")

                    # Handle Daily Report request
                    elif action == "get_daily_report":
                        self._logger.info("Frontend requested Daily Energy Report.")
                        report_data = self._generate_daily_report()
                        response = {"type": "daily_report", "data": report_data}
                        await websocket.send(json.dumps(response))

                    # Handle Weekly Report request
                    elif action == "get_weekly_report":
                        self._logger.info("Frontend requested Weekly Energy Report.")
                        report_data = self._generate_weekly_report()
                        response = {"type": "weekly_report", "data": report_data}
                        await websocket.send(json.dumps(response))

                    # Handle Monthly Report request
                    elif action == "get_monthly_report":
                        self._logger.info("Frontend requested Monthly Energy Report.")
                        report_data = self._generate_monthly_report()
                        response = {"type": "monthly_report", "data": report_data}
                        await websocket.send(json.dumps(response))
                            
                except json.JSONDecodeError:
                    self._logger.warning(f"Received malformed JSON from client: {message}")
                except Exception as e:
                    self._logger.error(f"Error handling incoming client message: {e}")

        except websockets.ConnectionClosed:
            # Client disconnected
            pass

        finally:
            # Remove client from active set
            self.clients.remove(websocket)
    
    async def _notify(self, message):
        """
        Send a message to all connected WebSocket clients.

        :param message: Serialized JSON message to broadcast.
        """
        if self.clients:
            await asyncio.gather(
                *[client.send(message) for client in self.clients],
                return_exceptions=True
            )

    def broadcast(self, nodes_dict):
        """
        Broadcast updated node data (including ONLINE, OFFLINE, PRINTING states) 
        to all connected clients.

        This method is thread-safe and schedules the async send operation inside the WebSocket event loop.

        :param nodes_dict: Dictionary containing the latest node state.
        """
        if self.loop and self.loop.is_running():
            asyncio.run_coroutine_threadsafe(
                self._notify(json.dumps(nodes_dict)),
                self.loop
            )

    # Helper function to create the initialization metrics for the Daily and Weekly reports
    def _report_init_metrics(self):
        return {
            "total_energy": 0.0,
            "wasted_energy": 0.0,
            "used_energy": 0.0,
            "prints_count": 0,
            "success_count": 0,
            "failed_count": 0
        }   

    # Helper function to calculate averages before sending to frontend
    def _calc_avgs(self, m):
        m["avg_per_print"] = m["total_energy"] / m["prints_count"] if m["prints_count"] > 0 else 0
        m["avg_success"] = m["used_energy"] / m["success_count"] if m["success_count"] > 0 else 0
        m["avg_failed"] = m["wasted_energy"] / m["failed_count"] if m["failed_count"] > 0 else 0
        return m

    def _generate_daily_report(self):
        """
        Queries the database to calculate daily energy metrics globally, 
        by printer, and by STL file.
        """
        # FIXED: Use self._db directly instead of relying on print_manager's internal variables
        if not self._db:
            self._logger.error("Database access not available for report generation.")
            return {"error": "Database access not available"}
            
        db = self._db
        
        # Aggregate power sum per print_id from the Measurement table, joined with the Print table
        query = """
            SELECT p.id, p.ip, p.stl_name, p.status, SUM(m.Power) as energy
            FROM `Print` p
            JOIN Measurement m ON p.id = m.print_id
            WHERE DATE(p.activation_time) = CURDATE()
            GROUP BY p.id, p.ip, p.stl_name, p.status
        """
        records = db.execute(query)
        
        global_metrics = self._report_init_metrics()
        by_printer = {}
        by_stl = {}
        
        if records:
            for row in records:
                # Support both dict and tuple return types depending on MySQL connector config
                if isinstance(row, dict):
                    ip = row.get('ip', 'Unknown')
                    stl = row.get('stl_name', 'Unknown')
                    status = row.get('status', 'UNKNOWN')
                    energy = float(row.get('energy') or 0.0)
                else:
                    ip = row[1]
                    stl = row[2]
                    status = row[3]
                    energy = float(row[4] or 0.0)
                    
                def update_metrics(m, stat, en):
                    m["total_energy"] += en
                    m["prints_count"] += 1
                    if stat == "FINISHED":
                        m["used_energy"] += en
                        m["success_count"] += 1
                    elif stat in ("FAILED", "ERROR"):
                        m["wasted_energy"] += en
                        m["failed_count"] += 1

                if ip not in by_printer: by_printer[ip] = self._report_init_metrics()
                if stl not in by_stl: by_stl[stl] = self._report_init_metrics()
                
                update_metrics(global_metrics, status, energy)
                update_metrics(by_printer[ip], status, energy)
                update_metrics(by_stl[stl], status, energy)
            
        return {
            "global": self._calc_avgs(global_metrics),
            "by_printer": {k: self._calc_avgs(v) for k, v in by_printer.items()},
            "by_stl": {k: self._calc_avgs(v) for k, v in by_stl.items()}
        }

    def _generate_weekly_report(self):
        """
        Queries the database to calculate weekly energy metrics (last 7 days) globally, 
        by printer, and by STL file.
        """
        if not self._db:
            self._logger.error("Database access not available for report generation.")
            return {"error": "Database access not available"}
            
        db = self._db
        
        # Aggregate power sum per print_id for the current calendar week (Monday to Sunday)
        query = """
            SELECT p.id, p.ip, p.stl_name, p.status, SUM(m.Power) as energy
            FROM `Print` p
            JOIN Measurement m ON p.id = m.print_id
            WHERE YEARWEEK(p.activation_time, 1) = YEARWEEK(CURDATE(), 1)
            GROUP BY p.id, p.ip, p.stl_name, p.status
        """
        records = db.execute(query)
            
        global_metrics = self._report_init_metrics()
        by_printer = {}
        by_stl = {}
        
        if records:
            for row in records:
                # Support both dict and tuple return types depending on MySQL connector config
                if isinstance(row, dict):
                    ip = row.get('ip', 'Unknown')
                    stl = row.get('stl_name', 'Unknown')
                    status = row.get('status', 'UNKNOWN')
                    energy = float(row.get('energy') or 0.0)
                else:
                    ip = row[1]
                    stl = row[2]
                    status = row[3]
                    energy = float(row[4] or 0.0)
                    
                def update_metrics(m, stat, en):
                    m["total_energy"] += en
                    m["prints_count"] += 1
                    if stat == "FINISHED":
                        m["used_energy"] += en
                        m["success_count"] += 1
                    elif stat in ("FAILED", "ERROR"):
                        m["wasted_energy"] += en
                        m["failed_count"] += 1

                if ip not in by_printer: by_printer[ip] = self._report_init_metrics()
                if stl not in by_stl: by_stl[stl] = self._report_init_metrics()
                
                update_metrics(global_metrics, status, energy)
                update_metrics(by_printer[ip], status, energy)
                update_metrics(by_stl[stl], status, energy)
            
        return {
            "global": self._calc_avgs(global_metrics),
            "by_printer": {k: self._calc_avgs(v) for k, v in by_printer.items()},
            "by_stl": {k: self._calc_avgs(v) for k, v in by_stl.items()}
        }

    def _generate_monthly_report(self):
        """
        Queries the database to calculate monthly energy metrics for the current year.
        Returns data aggregated by month (January to December).
        """
        if not self._db:
            self._logger.error("Database access not available for report generation.")
            return {"error": "Database access not available"}
            
        db = self._db
        
        # We group by print ID and Month to get accurate print counts and sum the power
        query = """
            SELECT p.id, MONTH(p.activation_time) as month_num, p.status, SUM(m.Power) as energy
            FROM `Print` p
            JOIN Measurement m ON p.id = m.print_id
            WHERE YEAR(p.activation_time) = YEAR(CURDATE())
            GROUP BY p.id, month_num, p.status
        """
        records = db.execute(query)
            
        # Initialize dictionary with all 12 months to guarantee consistent frontend rendering
        months_names = ["January", "February", "March", "April", "May", "June", 
                        "July", "August", "September", "October", "November", "December"]
        
        monthly_data = {
            month: {
                "prints_count": 0,
                "total_energy": 0.0,
                "used_energy": 0.0,
                "wasted_energy": 0.0
            } for month in months_names
        }
        
        if records:
            for row in records:
                if isinstance(row, dict):
                    month_num = row.get('month_num', 1)
                    status = row.get('status', 'UNKNOWN')
                    energy = float(row.get('energy') or 0.0)
                else:
                    month_num = row[1]
                    status = row[2]
                    energy = float(row[3] or 0.0)
                    
                # Map the database month number (1-12) to the month name
                if 1 <= month_num <= 12:
                    month_name = months_names[month_num - 1]
                    m = monthly_data[month_name]
                    
                    m["total_energy"] += energy
                    m["prints_count"] += 1
                    
                    if status == "FINISHED":
                        m["used_energy"] += energy
                    elif status in ("FAILED", "ERROR"):
                        m["wasted_energy"] += energy
            
        return monthly_data

    def run(self):
        """Start the WebSocket server with support for newer websockets versions."""
        # Create a dedicated loop for this thread
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)

        async def start_and_wait():
            # Start WebSocket server listening on all interfaces
            async with websockets.serve(self.handler, "0.0.0.0", 8765):
                self._logger.info("WebSocket Server is now listening on port 8765")
                # Keeps the server alive until the loop is stopped
                await asyncio.Future() 
        
        # Initialize server and keep the loop running indefinitely
        try:
            self.loop.run_until_complete(start_and_wait())
        except Exception as e:
            self._logger.error(f"WebSocket Loop error: {e}")
        finally:
            self.loop.close()