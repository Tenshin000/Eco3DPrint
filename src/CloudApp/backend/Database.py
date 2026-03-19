import mysql.connector
from mysql.connector import pooling
from utility.Log import Log

class Database:
    """
    Component for MySQL Database Access Layer (DAL).

    This class provides a robust interface for MySQL database interactions. 
    Its primary responsibilities include:
    - Managing the database connection lifecycle (establishing, verifying, and terminating).
    - Executing parameterized SQL queries securely to prevent SQL injection.
    - Providing comprehensive logging of all database operations and exceptions through a custom logging wrapper.
    - Utilizing Connection Pooling to allow safe, concurrent database access across multiple threads.
    """
    def __init__(self, host: str, user: str, password: str, database: str, pool_size: int = 5):
        """
        Initialize the Database instance with connection credentials and pool settings.

        :param host: The hostname or IP address of the MySQL server.
        :param user: The username for database authentication.
        :param password: The password for database authentication.
        :param database: The name of the specific database schema to access.
        :param pool_size: The number of connections to keep open in the pool (default: 5).
        """
        self._host = host
        self._user = user
        self._password = password
        self._database = database
        self._pool_size = pool_size

        # Initialize the custom logger for database operations
        self._logger = Log(
            logger_name="database_logger",
            module_name="DATABASE"
        ).get_logger()

        # Replaced single connection with a pool reference
        self._pool = None
        self._logger.info("Database access layer initialized with connection pooling.")

    def connect(self) -> bool:
        """
        Establish a connection pool to the MySQL database.

        This method is idempotent; it checks for an existing, active pool 
        before attempting to establish a new one.

        :return: True if the pool is successfully established or already active, 
                 False if an exception occurs during the initialization attempt.
        :rtype: bool
        """
        try:
            # Verify if a valid pool already exists
            if self._pool is not None:
                return True

            # Establish a new connection pool using provided credentials
            self._pool = mysql.connector.pooling.MySQLConnectionPool(
                pool_name="backend_pool",
                pool_size=self._pool_size,
                pool_reset_session=True, # Resets session variables when connection is returned
                host=self._host,
                user=self._user,
                password=self._password,
                database=self._database,
                use_pure=True
            )
            self._logger.info(f"Database connection pool (size: {self._pool_size}) successfully established.")
            return True

        except Exception as exc:
            self._logger.error(f"Failed to establish database connection pool. Exception: {exc}")
            self._pool = None
            return False

    def close(self) -> None:
        """
        Safely terminate the active database connection pool.

        This method ensures that the pool references are released. Any exceptions 
        raised during the closure process are suppressed to prevent application crashes 
        during teardown phases.
        """
        if self._pool:
            try:
                # The python mysql-connector pool doesn't have a hard close() method,
                # but removing the reference allows garbage collection to close sockets.
                self._pool = None
            except Exception:
                # Suppress exceptions during teardown to ensure safe exit
                pass
            
        self._logger.info("Database connection pool references released gracefully.")

    def execute(self, query: str, params: tuple = ()):
        """
        Execute a parameterized SQL query safely using a connection from the pool.

        This method handles both Data Query Language (DQL, e.g., SELECT) and 
        Data Manipulation Language (DML, e.g., INSERT, UPDATE, DELETE) statements. 
        It utilizes a buffered cursor to ensure all results are fetched client-side, 
        preventing "Unread result found" errors on subsequent queries.

        :param query: The SQL query string to be executed, utilizing `%s` placeholders.
        :param params: A tuple of parameters to be safely substituted into the query.
        :return: 
            - A list of tuples containing the fetched rows for SELECT queries.
            - True for non-SELECT queries that successfully commit.
            - None if the query execution fails or if no active connection exists.
        """
        # Validate the current pool state
        if not self._pool:
            self._logger.error("Query execution aborted: No active database connection pool.")
            return None

        connection = None
        cursor = None
        try:
            # Check out a connection from the pool
            connection = self._pool.get_connection()

            # Let's make sure the connection is still alive (avoid the "MySQL server has gone away" error)
            if not connection.is_connected():
                connection.ping(reconnect=True, attempts=3, delay=2)
            
            # Instantiate a buffered cursor to fetch all data immediately
            cursor = connection.cursor(buffered=True, dictionary=True)
            cursor.execute(query, params)

            # Evaluate if the query is expected to return rows (e.g., SELECT statement)
            if cursor.description is not None:
                # SELECT query 
                results = cursor.fetchall()
                return results

            # For DML queries (INSERT, UPDATE, DELETE), commit the transaction
            connection.commit()
            return True

        except Exception as exc:
            self._logger.error(f"SQL query execution failed. Exception: {exc}")
            return None
            
        finally:
            # The 'finally' block guarantees resources are freed even if exceptions occur!
            # Ensure the cursor is closed
            if cursor:
                try:
                    cursor.close()
                except Exception:
                    pass
            
            # Ensure the connection is returned to the pool!
            # Calling close() on a pooled connection does NOT close the socket, 
            # it just returns it to the pool for the next thread to use.
            if connection and connection.is_connected():
                try:
                    connection.close()
                except Exception:
                    pass