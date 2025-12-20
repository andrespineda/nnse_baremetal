# NNSE Module Diagram

This file contains the same module diagram embedded in `nnse_baremetal.c`, but in a Markdown file so editors that support Mermaid previews can render it directly.

```mermaid
graph TD
    A[Main Application] --> B[PDM Module]
    A --> C[I2S Module]
    A --> D[Speech Enhancement Module]
    A --> E[Button Module]
    A --> F[LED Module]
    A --> G[Timer Module]
    A --> H[Performance Profiler]
    D --> I[Neural Network Controller]
    I --> J[Feature Module]
    I --> K[NN Speech Module]
    I --> L[Neural Nets Module]
```

Open this file in VS Code and use a Mermaid preview extension (or GitHub's built-in Markdown preview) to view the rendered diagram.
