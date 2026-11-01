#pragma once

#include <string>

#include "logger.h"

unsigned int createShaderProgram(const std::string& vertexPath, const std::string& fragmentPath, Logger& logger);