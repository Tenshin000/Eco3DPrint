import os
import asyncio
import json
import queue
import threading
import tkinter as tk
import websockets
from tkinter import ttk, messagebox

from utility.Log import Log
from frontend.Printer import PrinterScreen

class UserApp:
    """Frontend User Application for managing 3D printers via WebSockets."""
    def __init__(self, root):
        """Initializes the main application window, variables, and background threads."""
        self.root = root

        self.root.title("Eco3DPrint - Dashboard")
        self.root.geometry("900x600")
        self.root.configure(bg="#1E1E1E")

        self.logger = Log(logger_name="gui_logger", module_name="FRONTEND").get_logger()
        self.ws_url = "ws://localhost:8765"

        self.COLORS = {
            "bg": "#1E1E1E",
            "header": "#2E7D32",
            "card_bg": "#2D2D2D",
            "card_hover": "#3D3D3D",
            "text_light": "#FFFFFF",
            "text_muted": "#AAAAAA",
            "filter_online": "#00FF00",
            "filter_offline": "#FF0000",
            "filter_printing": "#FFFF00" 
        }

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

        # Style the Notebook to match the dark theme (Hanging Folder effect)
        style = ttk.Style()
        style.theme_use('default')
        style.configure('TNotebook', background=self.COLORS["bg"], borderwidth=0)
        style.configure('TNotebook.Tab', background=self.COLORS["card_bg"], foreground=self.COLORS["text_light"], 
                        padding=[15, 5], font=('Helvetica', 12, 'bold'), borderwidth=0)
        style.map('TNotebook.Tab', background=[('selected', '#005CBF')]) # Blue highlight for selected tab

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

        # Space for future tabs (e.g., Settings, Analytics)
        # settings_frame = tk.Frame(self.notebook, bg=self.COLORS["bg"])
        # self.notebook.add(settings_frame, text="  Settings  ")

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
                # Route the message to the Printers screen
                if "printers" in self.screens:
                    self.screens["printers"].process_incoming_data(message)
                    
        except queue.Empty:
            pass

        self.root.after(100, self._process_queue)