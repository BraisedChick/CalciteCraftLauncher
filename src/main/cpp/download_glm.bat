@echo off
echo Downloading GLM library...

cd /d "%~dp0"

if not exist "third_party" mkdir third_party
if not exist "third_party\glm" (
    echo Cloning GLM from GitHub...
    git clone --depth 1 --branch 0.9.9.8 https://github.com/g-truc/glm.git third_party\glm
    if %errorlevel% equ 0 (
        echo GLM downloaded successfully!
    ) else (
        echo Failed to download GLM. Please check your internet connection.
        exit /b 1
    )
) else (
    echo GLM already exists in third_party\glm
)

echo Done!
pause
