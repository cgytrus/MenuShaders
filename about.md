# Menu Shaders
Replaces the background of the main menu with a customizable shader.

This is a port of mat's [Menu Shaders](https://github.com/matcool/small-gd-mods/blob/3e1783c7e281cbbccd53f9c4ceb697d5a6f839dd/src/menu-shaders.cpp) mod.

## Usage
To change the shader, create a texture pack with a file called menu-shader.fsh.

For a few already made shaders, check out this ~~Twitter~~ X [thread](https://twitter.com/mateus44_/status/1412108556921344006?s=20).

This mod also supports some Shadertoy shaders, to use them:
1. Find a shader you want to use on [Shadertoy](https://shadertoy.com)
2. Copy/paste the code on the right into your menu-shader.fsh
3. If you see the default GD background and errors from Menu Shaders in the console, that likely means that the shader you want to use is incompatible.

## Advanced Usage

### New shader locations
You can also replace shaders in `cgytrus.menu-shaders/any-frag.glsl` and `any-vert.glsl`
for the fragment and vertex shaders respectively,
where `any` can be replaced with either `main`, `level-select`, `creator`, `level-browser`,
`edit-level`, `play-level` or `search` to override the shader only for the respective menu.

Shadertoy compatibility is disabled in these shaders.

### Sprites
You can use sprites from resources (including custom ones) by adding a comment starting with `//@`
followed by sprite names separated by commas.

Each sprite in the list will add a `sampler2d` uniform called `sprite0`,
where `0` is the index of the sprite in the list.

### Nodes
You can get information about nodes in the scene by adding a comment starting with `//#`
followed by node IDs separated by commas.

Each node in the list will add a few uniforms, where `0` is the index of the sprite in the list:
- `vec2 node0Pos` - node position in cocos points in world space
- `float node0Rot` - node rotation in degrees in world space
- `vec2 node0Scale` - node scale in world space
- `vec2 node0Size` - node content size in cocos points in world space
- `bool node0Visible` - node visibility (whether it's drawn or not)
