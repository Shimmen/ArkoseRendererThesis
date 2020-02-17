#include "Shader.h"

#include "rendering/ShaderManager.h"
#include <utility/Logging.h>

ShaderFile::ShaderFile(std::string name, ShaderFileType type)
    : m_name(std::move(name))
    , m_type(type)
{
    auto& manager = ShaderManager::instance();
    switch (manager.loadAndCompileImmediately(m_name)) {
    case ShaderManager::ShaderStatus::FileNotFound:
        LogErrorAndExit("Shader file '%s' not found, exiting.\n");
    case ShaderManager::ShaderStatus::CompileError: {
        std::string errorMessage = manager.shaderError(m_name).value();
        LogError("Shader file '%s' has compile errors:\n", m_name.c_str());
        LogError("%s\n", errorMessage.c_str());
        LogErrorAndExit("Exiting due to bad shader at startup.\n");
    }
    default:
        break;
    }
}

const std::string& ShaderFile::name() const
{
    return m_name;
}

ShaderFileType ShaderFile::type() const
{
    return m_type;
}

Shader Shader::createVertexOnly(std::string name, std::string vertexName)
{
    ShaderFile vertexFile { std::move(vertexName), ShaderFileType::Vertex };
    return Shader(std::move(name), { vertexFile }, ShaderType::Raster);
}

Shader Shader::createBasic(std::string name, std::string vertexName, std::string fragmentName)
{
    ShaderFile vertexFile { std::move(vertexName), ShaderFileType::Vertex };
    ShaderFile fragmentFile { std::move(fragmentName), ShaderFileType::Fragment };
    return Shader(std::move(name), { vertexFile, fragmentFile }, ShaderType::Raster);
}

Shader Shader::createCompute(std::string name, std::string computeName)
{
    ShaderFile computeFile { std::move(computeName), ShaderFileType::Compute };
    return Shader(std::move(name), { computeFile }, ShaderType::Compute);
}

Shader::Shader(std::string name, std::vector<ShaderFile> files, ShaderType type)
    : m_name(std::move(name))
    , m_files(std::move(files))
    , m_type(type)
{
}

Shader::~Shader()
{
    if (!m_name.empty()) {
        // TODO: Maybe tell the resource manager that a shader was removed, so that it can reference count?
    }
}

ShaderType Shader::type() const
{
    return m_type;
}

const std::vector<ShaderFile>& Shader::files() const
{
    return m_files;
}
