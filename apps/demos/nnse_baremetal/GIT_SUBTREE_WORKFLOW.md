# Git Subtree Workflow for nnse_baremetal Demo

This document describes how to maintain the nnse_baremetal demo code within the NeuralSPOT structure while having a separate GitHub repository with only this code using Git subtree.

## Current Setup

The nnse_baremetal demo code is integrated into the NeuralSPOT structure at:
```
neuralSPOT/apps/demos/nnse_baremetal/
```

The standalone GitHub repository is at: https://github.com/andrespineda/nnse_baremetal

## Git Subtree Approach

We use Git subtree to extract only the nnse_baremetal content from the main repository and push it to the standalone repository.

### Initial Setup

The initial setup has already been completed. The standalone repository contains only the nnse_baremetal code extracted from the main NeuralSPOT repository.

### Updating the Standalone Repository

When you've made changes to the code in the NeuralSPOT structure and want to update the standalone repository:

1. Navigate to the NeuralSPOT root directory:
   ```bash
   cd /path/to/neuralSPOT-1.2.0-beta/neuralSPOT-1.2.0-beta
   ```

2. Run the update script:
   ```bash
   update_nnse_github.bat
   ```

This script will:
- Create a temporary directory for the standalone repo
- Initialize a git repository in the temporary directory
- Add the main repository as a remote
- Fetch from the main repository
- Checkout the master branch from the main repository
- Use git filter-branch to extract only the nnse_baremetal content
- Add the GitHub repository as origin
- Force push to GitHub
- Clean up the temporary directory

### Manual Update Process

If you prefer to update manually, follow these steps:

1. Create a temporary directory for the standalone repo:
   ```bash
   mkdir temp_nnse_standalone
   cd temp_nnse_standalone
   ```

2. Initialize git repository:
   ```bash
   git init
   ```

3. Add the main repository as a remote:
   ```bash
   git remote add neuralspot /path/to/neuralSPOT-1.2.0-beta/neuralSPOT-1.2.0-beta
   ```

4. Fetch from the main repository:
   ```bash
   git fetch neuralspot
   ```

5. Checkout the master branch from the main repository:
   ```bash
   git checkout -b temp_branch neuralspot/master
   ```

6. Use git filter-branch to extract only the nnse_baremetal content:
   ```bash
   set FILTER_BRANCH_SQUELCH_WARNING=1
   git filter-branch -f --prune-empty --subdirectory-filter apps/demos/nnse_baremetal HEAD
   ```

7. Add the GitHub repository as origin:
   ```bash
   git remote add origin https://github.com/andrespineda/nnse_baremetal.git
   ```

8. Force push to GitHub:
   ```bash
   git push -u origin temp_branch:master --force
   ```

9. Clean up:
   ```bash
   cd ..
   rmdir /s /q temp_nnse_standalone
   ```

## Benefits of This Approach

1. **Development Convenience**: Keep the code in the full NeuralSPOT structure for easy building and testing
2. **GitHub Repository Size**: Maintain a small, focused repository on GitHub
3. **Version Control**: Keep both repositories in sync
4. **Flexibility**: Easy to make changes in either location

## Important Notes

1. Make sure to exclude build artifacts and temporary files when syncing
2. Keep documentation files like README.md, BUILDING.md, etc. in the standalone repository
3. Update the standalone repository regularly to keep it in sync with NeuralSPOT
4. Consider using git tags to mark significant versions in both repositories