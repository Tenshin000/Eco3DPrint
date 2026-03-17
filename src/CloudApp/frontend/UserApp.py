import os
import asyncio
import json
import queue
import threading
import tkinter as tk
import websockets
from tkinter import ttk, messagebox

from frontend.DailyReport import DailyReportScreen
from frontend.MonthlyReport import MonthlyReportScreen
from frontend.WeeklyReport import WeeklyReportScreen
from frontend.Printers import PrinterScreen
from utility.Log import Log

class UserApp:
    """Frontend User Application for managing 3D printers via WebSockets."""
    def __init__(self, root):
        """Initializes the main application window, variables, and background threads."""
        self.root = root

        self.root.title("Eco3DPrint - Dashboard")
        self.root.geometry("1100x700") # Slightly wider to accommodate tables
        
        self.logger = Log(logger_name="gui_logger", module_name="FRONTEND").get_logger()
        self.ws_url = "ws://localhost:8765"

        # ECOLOGY-THEMED COLOR PALETTE
        self.COLORS = {
            "bg": "#121F17",              # Very dark forest green/black for background
            "header": "#1B5E20",          # Deep eco green for the header
            "card_bg": "#1E2D24",         # Slightly lighter green-gray for cards
            "card_hover": "#2B4034",      # Lighter green for hover states
            "text_light": "#E8F5E9",      # Very light green/white for primary text
            "text_muted": "#A5D6A7",      # Muted green for secondary text
            "button_bg": "#2E7D32",       # Eco green for buttons and highlights
            "filter_online": "#00FF00",   # Green color
            "filter_offline": "#FF0000",  # Red color
            "filter_printing": "#FFFF00"  # Yellow color
        }
        
        self.root.configure(bg=self.COLORS["bg"])

        self.is_running = True
        self.update_queue = queue.Queue()
        self.ws_loop = None
        self.current_ws = None
        
        # Dictionary to hold references to our tab screens
        self.screens = {}

        self._build_ui()
        self._process_queue()
        
        self.logger.info("Starting WebSocket background thread...")
        self._start_websocket_thread()

    def _build_ui(self):
        """Constructs the static structural components including the Tabbed Notebook."""
        # Header bar at the top of the window
        header = tk.Frame(self.root, bg=self.COLORS["header"], height=70)
        header.pack(fill=tk.X, side=tk.TOP)
        header.pack_propagate(False)

        title = tk.Label(
            header, text="Eco3DPrint Remote Management",
            bg=self.COLORS["header"], fg=self.COLORS["text_light"],
            font=("Helvetica", 20, "bold")
        )
        title.pack(side=tk.LEFT, padx=20, pady=15)

        # Style the Notebook to match the dark eco theme (Hanging Folder effect)
        style = ttk.Style()
        style.theme_use('default')
        style.configure('TNotebook', background=self.COLORS["bg"], borderwidth=0)
        style.configure('TNotebook.Tab', background=self.COLORS["card_bg"], foreground=self.COLORS["text_light"], 
                        padding=[15, 5], font=('Helvetica', 12, 'bold'), borderwidth=0)
        style.map('TNotebook.Tab', background=[('selected', self.COLORS["button_bg"])]) # Eco green highlight for selected tab

        # Main Notebook container
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=20, pady=(10, 20))

        # Tab 1: 3D Printers
        printers_frame = tk.Frame(self.notebook, bg=self.COLORS["bg"])
        self.notebook.add(printers_frame, text="  3D Printers  ")
        
        # Instantiate the PrinterScreen module inside the tab
        self.screens["printers"] = PrinterScreen(
            parent_frame=printers_frame, 
            root_window=self.root, 
            send_ws_callback=self.send_ws_message, 
            logger=self.logger, 
            colors=self.COLORS
        )

        # Tab 2: Daily Report
        report_frame = tk.Frame(self.notebook, bg=self.COLORS["bg"])
        self.notebook.add(report_frame, text="  Daily Report  ")
        
        # Instantiate the DailyReportScreen module inside the tab
        self.screens["report"] = DailyReportScreen(
            parent_frame=report_frame,
            send_ws_callback=self.send_ws_message,
            colors=self.COLORS
        )

        # Tab 3: Weekly Report
        weekly_frame = tk.Frame(self.notebook, bg=self.COLORS["bg"])
        self.notebook.add(weekly_frame, text="  Weekly Report  ")
        self.screens["weekly_report"] = WeeklyReportScreen(
            parent_frame=weekly_frame,
            send_ws_callback=self.send_ws_message,
            colors=self.COLORS
        )

        # Tab 4: Monthly Report 
        monthly_frame = tk.Frame(self.notebook, bg=self.COLORS["bg"])
        self.notebook.add(monthly_frame, text="  Monthly Report  ")
        self.screens["monthly_report"] = MonthlyReportScreen(
            parent_frame=monthly_frame,
            send_ws_callback=self.send_ws_message,
            colors=self.COLORS
        )

        # Bind the tab change event to automatically refresh data
        self.notebook.bind("<<NotebookTabChanged>>", self._on_tab_changed)

    def _on_tab_changed(self, event):
        """
        Triggered whenever the user switches tabs in the Notebook.
        Used to automatically refresh data for specific screens.
        """
        # Get the currently selected tab index
        selected_tab = self.notebook.index(self.notebook.select())
        
        # If the Daily Report tab (index 1) is selected, fetch the latest data
        if selected_tab == 1 and "report" in self.screens:
            self.logger.info("Daily Report tab selected. Auto-fetching latest data...")
            self.screens["report"].request_report()
        # If the Weekly Report tab (index 2) is selected, fetch the latest data
        elif selected_tab == 2 and "weekly_report" in self.screens:
            self.logger.info("Weekly Report tab selected. Auto-fetching latest data...")
            self.screens["weekly_report"].request_report()
        # If the Monthly Report tab (index 3) is selected 
        elif selected_tab == 3 and "monthly_report" in self.screens:
            self.logger.info("Monthly Report tab selected. Auto-fetching latest data...")
            self.screens["monthly_report"].request_report()

    def _start_websocket_thread(self):
        """Spawns a daemon thread to execute the asyncio WebSocket loop asynchronously."""
        ws_thread = threading.Thread(target=self._run_async_websocket, daemon=True)
        ws_thread.start()

    def _run_async_websocket(self):
        """Initializes and executes the asyncio event loop for the WebSocket client."""
        self.ws_loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.ws_loop)
        self.ws_loop.run_until_complete(self._websocket_listen())

    async def _websocket_listen(self):
        """Establishes a connection to the backend WebSocket and listens for push updates."""
        self.logger.info(f"Connecting to WebSocket at {self.ws_url}...")
        while self.is_running:
            try:
                async with websockets.connect(self.ws_url) as ws:
                    self.logger.info("WebSocket connected successfully!")
                    self.current_ws = ws 
                    
                    while self.is_running:
                        message = await ws.recv()
                        self.logger.debug(f"Received WS update: {message}")
                        self.update_queue.put(message)
                        
            except websockets.exceptions.ConnectionClosed:
                self.logger.warning("WebSocket connection closed. Reconnecting in 5s...")
                self.current_ws = None
                await asyncio.sleep(5)
            except Exception as e:
                self.logger.error(f"WebSocket error: {e}")
                self.current_ws = None
                await asyncio.sleep(5)

    def send_ws_message(self, message_dict):
        """Thread-safe method to serialize and send a JSON request to the backend server."""
        if self.current_ws and self.ws_loop:
            coro = self.current_ws.send(json.dumps(message_dict))
            asyncio.run_coroutine_threadsafe(coro, self.ws_loop)
            self.logger.info(f"Sent request to backend: {message_dict}")
        else:
            self.logger.error("Cannot send message: WebSocket is disconnected.")
            messagebox.showerror("Network Error", "Not connected to the backend server.")

    def _process_queue(self):
        """Periodically checks the queue for new telemetry data and safely routes it."""
        if not self.is_running:
            return

        try:
            while True:
                message = self.update_queue.get_nowait()
                parsed_data = json.loads(message)
                
                # Intelligent routing to the correct screen based on the message "type"
                if "type" in parsed_data:
                    if parsed_data["type"] == "daily_report" and "report" in self.screens:
                        self.screens["report"].process_incoming_data(message)
                    elif parsed_data["type"] == "weekly_report" and "weekly_report" in self.screens:
                        self.screens["weekly_report"].process_incoming_data(message)
                    elif parsed_data["type"] == "monthly_report" and "monthly_report" in self.screens:
                        self.screens["monthly_report"].process_incoming_data(message)
                    elif parsed_data["type"] == "stl_list" and "printers" in self.screens:
                        self.screens["printers"].process_incoming_data(message)
                else:
                    # If there's no "type", it's the standard active_nodes status dictionary
                    if "printers" in self.screens:
                        self.screens["printers"].process_incoming_data(message)    
        except queue.Empty:
            pass
        except json.JSONDecodeError:
            self.logger.error("Failed to decode JSON from queue.")

        self.root.after(100, self._process_queue)