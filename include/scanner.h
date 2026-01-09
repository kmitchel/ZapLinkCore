#ifndef SCANNER_H
#define SCANNER_H

// Checks if channels.conf exists.
// If it implies missing, it prompts the user to run the setup wizard.
// If the wizard runs successfully, this function returns 1 (indicating restart/exit needed).
// If the wizard is skipped or fails, or config exists, it returns 0 (continue normal startup).
int scanner_check(const char *config_path);

#endif
