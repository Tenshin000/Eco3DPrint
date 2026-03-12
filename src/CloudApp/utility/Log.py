import logging


class ModuleFilter(logging.Filter):
    """Logging filter that injects a dynamic 'module_label' attribute into each log record."""
    def __init__(self, module_name: str):
        super().__init__()
        self._module_name = module_name

    def filter(self, record: logging.LogRecord) -> bool:
        # Attach the custom module label to the log record
        record.module_label = self._module_name
        return True

    def set_module(self, module_name: str) -> None:
        # Update the module label at runtime
        self._module_name = module_name


class Log:
    """
    Logging wrapper that:
    - Adds a custom module label
    - Formats output as: Timestamp - Module: Message
    - Optionally prevents duplicate log propagation
    """
    def __init__(self, logger_name: str, module_name: str, level: int = logging.INFO, allow_propagation: bool = False):
        # Retrieve or create the logger
        self._logger = logging.getLogger(logger_name)
        self._logger.setLevel(level)

        # Control log propagation to parent loggers
        self._logger.propagate = allow_propagation

        # Create and attach the custom filter
        self._module_filter = ModuleFilter(module_name)

        # Avoid adding multiple handlers if already configured
        if not self._logger.handlers:
            handler = logging.StreamHandler()

            formatter = logging.Formatter(
                fmt="%(asctime)s - %(module_label)s: %(message)s",
                datefmt="%Y-%m-%d %H:%M:%S"
            )

            handler.setFormatter(formatter)
            self._logger.addHandler(handler)

        # Ensure the filter is added only once
        if self._module_filter not in self._logger.filters:
            self._logger.addFilter(self._module_filter)

    def set_level(self, level: int) -> None:
        """Dynamically update the logging level."""
        self._logger.setLevel(level)

    def set_propagation(self, allow_propagation: bool) -> None:
        """Enable or disable propagation to parent loggers."""
        self._logger.propagate = allow_propagation

    def set_module(self, module_name: str) -> None:
        """Update the module label dynamically."""
        self._module_filter.set_module(module_name)

    def get_logger(self) -> logging.Logger:
        """Return the configured logger instance."""
        return self._logger
    