from coapthon.server.coap import CoAP

from backend.Database import Database
from backend.NodeMonitor import NodeMonitor
from backend.PrintManager import PrintManager
from .resources.NodeRegistration import NodeRegistration
from .resources.PrintFinished import PrintFinished
from utility.Log import Log

class CoAPServer(CoAP):
    """Component for a Custom CoAP server with IPv6 support."""
    def __init__(self, host="::", port=5683, multicast=False, database=None, monitor=None, manager=None):
        """
        host: str, default "::" binds all IPv6 addresses
        port: int, CoAP standard UDP port 5683
        """
        super().__init__((host, port), multicast=multicast)

        if database is None:
            # Load database configuration parameters
            config = ConfigParser()
            config.read('./backend/config.ini')
            db_host = config.get('mysql', 'host')
            db_user = config.get('mysql', 'user')
            db_password = config.get('mysql', 'password')
            db_name = config.get('mysql', 'database')
            
            self._db = Database(host=db_host, user=db_user, password=db_password, database=db_name)
            
            if not self._db.connect():
                self._logger.error("Fatal Error: Unable to connect to the database. Exiting application.")
                exit(1)
        else:
            self._db = database
        
        if monitor is None:
            monitor = NodeMonitor()

        if manager is None:
            manager = PrintManager()

        self.add_resource("/registration", NodeRegistration(database=database, monitor=monitor))
        self.add_resource("/printFinished", PrintFinished(print_manager=manager))
        
        self._logger = Log(
            logger_name="server_logger",
            module_name="CoAP_SERVER"
        ).get_logger()
        self._logger.info(f"CoAP Server Initialized on [{host}]:{port}...")
        