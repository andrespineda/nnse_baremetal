@echo off
:: update_nnse_github.bat
:: Script to update the standalone GitHub repository with changes from the main NeuralSPOT repository

echo Updating nnse_baremetal standalone repository...

:: Create a temporary directory for the standalone repo
mkdir temp_nnse_standalone
cd temp_nnse_standalone

:: Initialize git repository
git init

:: Add the main repository as a remote
git remote add neuralspot ..

:: Fetch from the main repository
git fetch neuralspot

:: Checkout the master branch from the main repository
git checkout -b temp_branch neuralspot/master

:: Use git filter-branch to extract only the nnse_baremetal content
set FILTER_BRANCH_SQUELCH_WARNING=1
git filter-branch -f --prune-empty --subdirectory-filter apps/demos/nnse_baremetal HEAD

:: Add the GitHub repository as origin
git remote add origin https://github.com/andrespineda/nnse_baremetal.git

:: Force push to GitHub
git push -u origin temp_branch:master --force

:: Clean up
cd ..
rmdir /s /q temp_nnse_standalone

echo Update completed!