# Contributing to Keyboard Layout Overlay

Thank you for your interest in contributing to the Keyboard Layout Overlay project! This document provides guidelines and information for contributors.

## 🚀 Quick Start

1. **Fork** the repository on GitHub
2. **Clone** your fork locally
3. **Build** the project:
   ```bash
   mkdir build && cd build
   cmake ..
   make                    # macOS/Linux
   # or
   cmake --build .         # Windows
   ```
4. **Test** your changes:
   ```bash
   ./test_mvp
   ./test_overlay_copy
   ./test_overlay_scale
   ```

## 🏗️ Architecture Overview

This project uses a **98% shared codebase** with enterprise-level abstraction layers:

### Core Abstraction Layers (shared/)
- `platform.h` - Basic platform API abstraction
- `event_system.h` - Cross-platform event handling
- `file_system.h` - Unified file operations
- `graphics.h` - Rendering abstraction layer
- `threading.h` - Multi-threading utilities
- `system_info.h` - Hardware/system information
- `timer.h` - High-precision timing

### Platform Implementations
- `windows/` - Win32 implementation
- `macos/` - Cocoa implementation

## 📝 Development Guidelines

### Code Style
- **C11 standard** with clean, readable code
- **Consistent naming** - use `snake_case` for functions/variables
- **Clear comments** - document complex logic
- **Error handling** - check return values and handle errors gracefully

### Platform-Specific Code
- **Minimize platform code** - keep it under 5% of total codebase
- **Use abstraction layers** - don't add platform-specific code to shared/
- **Document platform differences** - comment OS-specific behavior

### Testing
- **Write tests** for new functionality
- **Run all tests** before submitting PR
- **Cross-platform testing** - ensure it works on both Windows and macOS

## 🔧 Making Changes

### Adding New Features
1. **Check existing abstractions** - see if you can extend current layers
2. **Add to shared code** - prefer shared implementations over platform-specific
3. **Update abstractions** - if needed, extend existing abstraction layers
4. **Test thoroughly** - ensure cross-platform compatibility

### Platform-Specific Changes
1. **Use abstraction layers** - implement platform-specific logic in platform files
2. **Document differences** - comment platform-specific behavior
3. **Test both platforms** - ensure changes work on Windows and macOS

### Abstraction Layer Extensions
1. **Extend existing layers** - add to current abstraction headers
2. **Update implementations** - modify platform-specific implementations
3. **Maintain compatibility** - don't break existing functionality

## 🧪 Testing

### Running Tests
```bash
cd build
./test_mvp
./test_overlay_copy
./test_overlay_scale
```

### CI/CD Pipeline
The project uses GitHub Actions for automated testing:
- **Quality checks** - validates code quality and temporary files
- **Cross-platform builds** - tests on Windows and macOS
- **Architecture validation** - ensures ≥90% shared code
- **Abstraction verification** - confirms all 7 layers exist

## 📋 Pull Request Process

1. **Create a branch** from `main`
2. **Make your changes** following the guidelines above
3. **Test thoroughly** on both platforms
4. **Update documentation** if needed
5. **Submit a PR** with a clear description
6. **Address review feedback**

### PR Requirements
- ✅ **All tests pass**
- ✅ **Cross-platform compatibility**
- ✅ **Documentation updated**
- ✅ **No temporary files**
- ✅ **Clean commit history**

## 🎯 Architecture Principles

### Shared Code First
- **98% shared codebase** - maximize shared functionality
- **Platform abstractions** - use the 7 abstraction layers
- **Minimal platform code** - keep platform-specific code minimal

### Future-Proof Design
- **Extensible abstractions** - easy to add new platforms
- **Clean interfaces** - well-defined API boundaries
- **Modular architecture** - independent components

### Quality Standards
- **Enterprise-level code** - professional quality standards
- **Comprehensive testing** - thorough test coverage
- **Documentation** - clear and complete docs

## 🆘 Getting Help

- **Issues** - Use GitHub Issues for bugs and feature requests
- **Discussions** - Use GitHub Discussions for questions
- **Documentation** - Check README.md and this file first

## 📄 License

By contributing to this project, you agree that your contributions will be licensed under the same MIT License that covers the project.

Thank you for contributing to Keyboard Layout Overlay! 🎉
