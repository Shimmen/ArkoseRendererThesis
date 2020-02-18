#include "Shader.h"

#include "rendering/ShaderManager.h"
#include <utility/Logging.h>

ShaderFile::ShaderFile(std::string path, ShaderFileType type)
    : m_path(std::move(path))
    , m_type(type)
{
    auto& manager = ShaderManager::instance();
    switch (manager.loadAndCompileImmediately(m_path)) {
    case ShaderManager::ShaderStatus::FileNotFound:
        LogErrorAndExit("Shader file '%s' not found, exiting.\n");
    case ShaderManager::ShaderStatus::CompileError: {
        std::string errorMessage = manager.shaderError(m_path).value();
        LogError("Shader file '%s' has compile errors:\n", m_path.c_str());
        LogError("%s\n", errorMessage.c_str());
        LogErrorAndExit("Exiting due to bad shader at startup.\n");
    }
    default:
        break;
    }
}

const std::string& ShaderFile::path() const
{
    return m_path;
}

ShaderFileType ShaderFile::type() const
{
    return m_type;
}

Shader Shader::createVertexOnly(std::string vertexName)
{
    ShaderFile vertexFile { std::move(vertexName), ShaderFileType::Vertex };
    return Shader({ vertexFile }, ShaderType::Raster);
}

Shader Shader::createBasic(std::string vertexName, std::string fragmentName)
{
    ShaderFile vertexFile { std::move(vertexName), ShaderFileType::Vertex };
    ShaderFile fragmentFile { std::move(fragmentName), ShaderFileType::Fragment };
    return Shader({ vertexFile, fragmentFile }, ShaderType::Raster);
}

Shader Shader::createCompute(std::string computeName)
{
    ShaderFile computeFile { std::move(computeName), ShaderFileType::Compute };
    return Shader({ computeFile }, ShaderType::Compute);
}

Shader::Shader(std::vector<ShaderFile> files, ShaderType type)
    : m_files(std::move(files))
    , m_type(type)
{
}

Shader::~Shader()
{
    // TODO: Maybe tell the resource manager that a shader was removed, so that it can reference count?
}

ShaderType Shader::type() const
{
    return m_type;
}

const std::vector<ShaderFile>& Shader::files() const
{
    return m_files;
}
