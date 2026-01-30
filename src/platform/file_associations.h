#pragma once

#include <QString>

class FileAssociations {
public:
  // Returns true if this app appears to be registered as a handler for .pak files.
  static bool is_pak_registered(QString* details);

  // Registers this app as a handler for .pak files (best-effort).
  // Note: On modern Windows, the user may still need to choose PakFu in "Default apps".
  static bool apply_pak_registration(QString* error);
};

