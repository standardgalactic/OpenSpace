/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2021                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#ifndef __OPENSPACE_CORE___TEXTURECOMPONENT___H__
#define __OPENSPACE_CORE___TEXTURECOMPONENT___H__

#include <ghoul/opengl/ghoul_gl.h>
#include <ghoul/opengl/texture.h>
#include <optional>

namespace ghoul::filesystem { class File; }
namespace ghoul::opengl {class Texture; }

namespace openspace {

class TextureComponent {
    using Texture = ghoul::opengl::Texture;

public:
    TextureComponent() = default;
    TextureComponent(const Texture::FilterMode filterMode, bool watchFile = true);
    ~TextureComponent() = default;

    Texture* texture() const;

    void bind();
    void uploadToGpu();

    // Loads a texture from a file on disk
    void loadFromFile(const std::string& path);

    // Function to call in a renderable's update function to make sure
    // the texture is kept up to date
    void update();

private:
    std::unique_ptr<ghoul::filesystem::File> _textureFile = nullptr;
    std::unique_ptr<Texture> _texture = nullptr;

    Texture::FilterMode _filterMode = Texture::FilterMode::LinearMipMap;
    bool _shouldWatchFile = true;

    bool _fileIsDirty = false;
    bool _textureIsDirty = false;
};

} // namespace openspace

#endif // __OPENSPACE_CORE___TEXTURECOMPONENT___H__
