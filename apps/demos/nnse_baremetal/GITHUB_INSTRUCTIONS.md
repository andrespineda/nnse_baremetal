# GitHub Repository Setup Instructions

This document provides step-by-step instructions to push the local nnse_baremetal repository to GitHub.

## Prerequisites

1. Git must be installed on your system
2. You must have a GitHub account
3. You've already created an empty repository on GitHub named `nnse_baremetal`

## Steps to Push Local Repository to GitHub

### 1. Navigate to the local repository
Open a terminal/command prompt and navigate to the repository directory:
```bash
cd c:\Users\apineda\Downloads\neuralSPOT-1.2.0-beta\neuralSPOT-1.2.0-beta\nnse_baremetal
```

### 2. Add the remote origin
Add the GitHub repository as the remote origin (replace `your-username` with your actual GitHub username):
```bash
git remote add origin https://github.com/your-username/nnse_baremetal.git
```

### 3. Verify the remote has been added
```bash
git remote -v
```
You should see the origin URL listed.

### 4. Push the repository to GitHub
Push all branches and tags to GitHub:
```bash
git push -u origin main
```

If your default branch is named differently (e.g., master), use:
```bash
git push -u origin master
```

### 5. Verify on GitHub
After the push completes, refresh your GitHub repository page to confirm that all files have been uploaded.

## Troubleshooting

### If you get authentication errors:
1. Use GitHub CLI for authentication:
   ```bash
   gh auth login
   ```

2. Or use a Personal Access Token:
   ```bash
   git remote set-url origin https://your-username:your-personal-access-token@github.com/your-username/nnse_baremetal.git
   ```

### If the push fails because the remote repository has changes:
1. Fetch and merge remote changes:
   ```bash
   git fetch origin
   git merge origin/main
   ```
   
2. Resolve any conflicts and then push again.

## Additional Git Commands

To check the status of your repository:
```bash
git status
```

To see commit history:
```bash
git log --oneline
```