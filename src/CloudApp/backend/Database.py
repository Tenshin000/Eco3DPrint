import mysql.connector

from utility.Log import Log

class Database:
    """
    Component for MySQL Database Access Layer (DAL).

    This class provides a robust interface for MySQL database interactions. 
    Its primary responsibilities include:
    - Managing the database connection lifecycle (establishing, verifying, and terminating).
    - Executing parameterized SQL queries securely to prevent SQL injection.
    - Providing comprehensive logging of all database operations and exceptions through a custom logging wrapper.
    """
    def __init__(self, host: str, user: str, password: str, database: str):
        """
        Initialize the Database instance with connection credentials.

        :param host: The hostname or IP address of the MySQL server.
        :param user: The username for database authentication.
        :param password: The password for database authentication.
        :param database: The name of the specific database schema to access.
        """
        self._host = host
        self._user = user
        self._password = password
        self._database = database

        # Initialize the custom logger for database operations
        self._logger = Log(
            logger_name="database_logger",
            module_name="DATABASE"
        ).get_logger()

        self._connection = None
        self._logger.info("Database access layer initialized.")

    def connect(self) -> bool:
        """
        Establish a connection to the MySQL database.

        This method is idempotent; it checks for an existing, active connection 
        before attempting to establish a new one.

        :return: True if the connection is successfully established or already active, 
                 False if an exception occurs during the connection attempt.
        :rtype: bool
        """
        try:
            # Verify if a valid connection already exists
            if self._connection and getattr(self._connection, "is_connected", lambda: False)():
                return True

            # Establish a new connection using provided credentials
            self._connection = mysql.connector.connect(
                host=self._host,
                user=self._user,
                password=self._password,
                database=self._database,
                use_pure=True
            )
            self._logger.info("Database connection successfully established.")
            return True

        except Exception as exc:
            self._logger.error(f"Failed to establish database connection. Exception: {exc}")
            self._connection = None
            return False

    def close(self) -> None:
        """
        Safely terminate the active database connection.

        This method ensures that the connection is gracefully closed. Any exceptions 
        raised during the closure process are suppressed to prevent application crashes 
        during teardown phases.
        """
        if self._connection:
            try:
                self._connection.close()
            except Exception:
                # Suppress exceptions during teardown to ensure safe exit
                pass
            
        self._connection = None
        self._logger.info("Database connection closed gracefully.")

    def execute(self, query: str, params: tuple = ()):
        """
        Execute a parameterized SQL query safely.

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
        # Validate the current connection state
        if not self._connection or not getattr(self._connection, "is_connected", lambda: False)():
            self._logger.error("Query execution aborted: No active database connection.")
            return None

        cursor = None
        try:
            # Instantiate a buffered cursor to fetch all data immediately
            cursor = self._connection.cursor(buffered=True)
            cursor.execute(query, params)

            # Evaluate if the query is expected to return rows (e.g., SELECT statement)
            if getattr(cursor, "with_rows", False):
                results = cursor.fetchall()
                cursor.close()
                return results

            # For DML queries (INSERT, UPDATE, DELETE), commit the transaction
            self._connection.commit()
            cursor.close()
            return True

        except Exception as exc:
            self._logger.error(f"SQL query execution failed. Exception: {exc}")
            
            # Ensure the cursor is closed even if an exception occurs
            try:
                if cursor:
                    cursor.close()
            except Exception:
                pass
                
            return None