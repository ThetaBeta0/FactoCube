#include "render/shader.h"

#include <fstream>
#include <iostream>
#include <iterator>

#include <glad/glad.h>

unsigned int createShaderProgram(const std::string& vertexPath, const std::string& fragmentPath, Logger& logger) {
    std::string vertexShaderSource;
    std::string fragmentShaderSource;
    {
        std::ifstream vShaderFile(vertexPath);
        std::ifstream fShaderFile(fragmentPath);
        if (!vShaderFile.is_open() || !fShaderFile.is_open()) {
            logger.logError("Failed to open shader files: " + vertexPath + ", " + fragmentPath);
            return 0;
        }
        vertexShaderSource.assign((std::istreambuf_iterator<char>(vShaderFile)), std::istreambuf_iterator<char>());
        fragmentShaderSource.assign((std::istreambuf_iterator<char>(fShaderFile)), std::istreambuf_iterator<char>());
    }


    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char* vShaderCode = vertexShaderSource.c_str();
    glShaderSource(vertexShader, 1, &vShaderCode, 0);
    glCompileShader(vertexShader);
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertexShader, 512, 0, infoLog);
        std::cerr << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }   

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fShaderCode = fragmentShaderSource.c_str();
    glShaderSource(fragmentShader, 1, &fShaderCode, 0);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, 0, infoLog);
        std::cerr << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, 0, infoLog);
        std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        logger.logError("Shader program failed to link: " + vertexPath);
        glDeleteProgram(shaderProgram);
        return 0;
    }

    return shaderProgram;
}