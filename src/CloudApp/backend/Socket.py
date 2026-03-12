import os
import asyncio
import json
import websockets

from backend.PrintManager import PrintManager 
from utility.Log import Log


class WebSocketManager:
    """
    Manages WebSocket connections used to push real-time updates
    from the backend to connected frontend clients, and handles
    incoming requests from the frontend.
    """
    def __init__(self, get_state_func, print_manager=None):
        """
        Initialize the WebSocket manager.

        :param get_state_func: Function that returns the current system state.
        :param print_manager: Reference to the PrintManager component.
        """
        # Set of currently connected WebSocket clients
        self.clients = set()
        # Event loop used by the WebSocket server
        self.loop = None
        # Function used to retrieve the latest node state
        self.get_state_func = get_state_func
        if print_manager is None:
            self. print_manager = PrintManager()
        else:
            self.print_manager = print_manager
        # Logger for WebSocket server events
        self._logger = Log(
            logger_name="socket_logger",
            module_name="SOCKET_SERVER"
        ).get_logger()

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
                    
                    # Check if the frontend is requesting the list of STL files
                    if data.get("action") == "get_stls":
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
                    elif data.get("action") == "send_stl":
                        target_ip = data.get("ip")
                        filename = data.get("file")
                        
                        if target_ip and filename and self.print_manager:
                            self._logger.info(f"Frontend requested STL transfer. Delegating to PrintManager.")
                            # Pass the request to the PrintManager
                            self.print_manager.add_print_job(target_ip, filename)
                        else:
                            self._logger.warning("Received invalid 'send_stl' command or PrintManager missing.")
                            
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