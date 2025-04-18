# CurveMaker 📈

A versatile curve editor built designed for creating multi-channel easing curves and exporting them as 1D LUTs (Lookup Textures), primarily targeting game development and shader animation workflows.

![Screenshot 2025-04-17 181323](https://github.com/user-attachments/assets/1d5af709-f87e-4d0c-9e5f-f32efece3811)




## :question: What? Why?

Shader-based vertex animations are highly performant for effects like swaying trees, vegetation, destruction, and more. But standard sine waves limit artistic control if you need stylized movement. Animators prefer intuitive easing curves, which aren't native to shaders. [While mathematical easing functions are possible](https://docs.google.com/document/d/1x9gch_zQ6Farvp-Q6R4_FhWrpbcybUW2fCfVUCdPZvs/edit?usp=sharing), they can be complex and potentially impact GPU performance.

Curve Maker solves this. It provides an easy method to encode your custom animation curves into Look-Up Tables (LUTs). You can then sample these LUTs efficiently in your shader, using Time to drive animations. This approach is very performant, mainly costing the memory footprint of the LUT and the texture fetch operation.

## ▶️Showcase 
[![IMAGE ALT TEXT](https://github.com/user-attachments/assets/d9098f51-96b0-4df3-a8fb-ca38fa8b8d25)
](https://youtu.be/ombsLh9y2nA "Showcase")

## ✨ Features

* **Multi-Channel Bézier Curve Editing:**
    * Independently edit curves for Red, Green, and Blue channels.
    * Add, delete, and drag curve nodes (main points).
    * Manipulate Bézier control handles for precise curve shaping.
    * Handle Alignment Modes: Free, Aligned, Mirrored.
* **Selection Tools:**
    * Single-click selection for nodes and handles.
    * Multi-select main nodes using Shift+Click.
    * Box (marquee) selection for main nodes (Hold Shift to add to selection).
    * Drag selected nodes together.
    * Delete selected nodes (via Delete key or Right-Click on node).
* **Export Options:**
    * **1D Combined RGB LUT:** Export the R, G, B curves into a single `Width x 1` pixel texture (8-bit or 16-bit PNG). Ideal for sampling three easing values simultaneously in shaders based on time (U-coordinate).
* **Live Previews:**
    * **LUT Preview:** See a real-time gradient preview of the generated LUT.
    * **Animation Preview:** Watch an object animate vertically based on the *active channel's* curve output over a looping time period. Helps visualize the easing effect.
* **Save/Load:**
    * Save the complete state of all R, G, B curves and associated UI settings (LUT size, export bit depth, view options) to a JSON project file (`.json`).
    * Load previously saved curve projects.
* **Customization & UI:**
    * Undo/Redo support for most actions.
    * Toggle visibility of inactive curve channels in the background.
    * Optionally clamp control handles within the [0, 1] canvas area.
    * Switch between Light and Dark themes.
    * Configurable LUT width and export bit depth (8/16 bit per channel).

## 🛠️ Technology Stack

* C++ (C++17 or later recommended)
* Qt Framework (Developed with Qt 6.5+, likely compatible with recent Qt 6 versions)
* CMake (Build System)

## 🚀 Getting Started

### Prerequisites

* **Qt 6:** You need Qt 6 installed (version 6.5 or later recommended). Make sure to install the modules: Core, GUI, Widgets.
* **CMake:** Version 3.19 or later.
* **C++ Compiler:**
    * **Windows:** MinGW (provided with Qt installer) or MSVC (Visual Studio 2019 or later).
    * **macOS:** Xcode Command Line Tools (provides Clang). Run `xcode-select --install` if needed.
    * **Linux:** GCC or Clang.

### Building from Source

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/omriyaloz/Curve-Maker.git
    cd CurveMaker
    ```
2.  **Configure using CMake:** Create a build directory.
    ```bash
    mkdir build
    cd build
    # On Windows/Linux (adjust generator if needed):
    cmake ..
    # On macOS:
    cmake .. -G Xcode # Or use default Makefile generator
    ```
    *(Note: If Qt isn't found automatically, you might need to set `CMAKE_PREFIX_PATH` e.g., `cmake .. -DCMAKE_PREFIX_PATH=C:/Qt/6.x.y/mingw_64`)*
3.  **Build the project:**
    ```bash
    cmake --build . --config Release # Or Debug
    ```
    *(Alternatively, open the generated project file in Qt Creator or Visual Studio/Xcode and build from there)*

### Running a Pre-built Version

[**Release v1.0**](https://github.com/omriyaloz/Curve-Maker/releases/tag/v1.0.0)

* **Windows:** Download the `.zip` file. Extract it. Run `CurveMaker.exe`. You *might* need to install the corresponding Microsoft VC++ Redistributable package if it was built with MSVC (check release notes).


## 📖 Usage

1.  **Select Channel:** Use the R, G, B radio buttons to choose the curve channel you want to edit or preview (in single-channel mode).
2.  **Edit Curve:**
    * **Add Node:** Left-click on a curve segment.
    * **Select Node(s):** Left-click on a main point. Shift+Click to add/remove from selection. Drag a box in empty space to select contained nodes (Shift+Drag to add).
    * **Move Node(s):** Click and drag a selected main point. All selected points move together.
    * **Edit Handles:** Click and drag the small handle points connected to a main node.
    * **Delete Node(s):** Select node(s) and press the `Delete` key, or Right-Click on a single node. (Endpoints cannot be deleted).
    * **Change Alignment:** Select a single intermediate node and use the `F`, `A`, `M` keys or the corresponding buttons to set handle alignment (Free, Aligned, Mirrored).
3.  **Preview:** Observe the LUT Preview and the Animation Preview (updates based on the active channel). Use the "View" menu for more view options.
4.  **Export/Generate:**
    * Configure LUT Width and Export Bit Depth in the "LUT" tab.
    * Click "Export LUT" to save the 1D Combined RGB texture.
5.  **Save/Load:** Use the File menu to save your current curves and settings to a `.json` file or load a previous project.


## 📜 License
Distributed under the MIT License. See LICENSE file for more information.
