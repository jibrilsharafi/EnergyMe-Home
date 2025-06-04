// Authentication helper for EnergyMe-Home
class AuthManager {
    constructor() {
        this.token = this.getStoredToken();
        this.checkAuthOnLoad();
    }

    getStoredToken() {
        return localStorage.getItem('auth_token') || '';
    }

    setToken(token) {
        this.token = token;
        localStorage.setItem('auth_token', token);
    }

    clearToken() {
        this.token = '';
        localStorage.removeItem('auth_token');
    }

    getAuthHeaders() {
        if (this.token) {
            return {
                'Authorization': `Bearer ${this.token}`,
                'Content-Type': 'application/json'
            };
        }
        return {
            'Content-Type': 'application/json'
        };
    }

    async checkAuthStatus() {
        try {
            const response = await fetch('/rest/auth/status', {
                method: 'GET',
                headers: this.getAuthHeaders()
            });

            if (response.ok) {
                const data = await response.json();
                return data.authenticated;
            }
            return false;
        } catch (error) {
            console.error('Auth check failed:', error);
            return false;
        }
    }

    async checkAuthOnLoad() {
        // Skip auth check for login page
        if (window.location.pathname === '/login') {
            return;
        }

        const isAuthenticated = await this.checkAuthStatus();
        if (!isAuthenticated) {
            this.redirectToLogin();
        }
    }

    redirectToLogin() {
        window.location.href = '/login';
    }

    async logout() {
        try {
            await fetch('/rest/auth/logout', {
                method: 'POST',
                headers: this.getAuthHeaders()
            });
        } catch (error) {
            console.error('Logout failed:', error);
        } finally {
            this.clearToken();
            this.redirectToLogin();
        }
    }

    async apiCall(url, options = {}) {
        const defaultOptions = {
            headers: this.getAuthHeaders()
        };

        const mergedOptions = {
            ...defaultOptions,
            ...options,
            headers: {
                ...defaultOptions.headers,
                ...options.headers
            }
        };

        try {
            const response = await fetch(url, mergedOptions);

            if (response.status === 401) {
                this.clearToken();
                this.redirectToLogin();
                return null;
            }

            return response;
        } catch (error) {
            console.error('API call failed:', error);
            throw error;
        }
    }
}

// Global auth manager instance
window.authManager = new AuthManager();

// Backward compatibility - create authAPI object that mimics the old interface
window.authAPI = {
    async get(url, options = {}) {
        const response = await window.authManager.apiCall(url, {
            method: 'GET',
            ...options
        });
        if (response && response.ok) {
            return await response.json();
        }
        throw new Error(`HTTP ${response?.status}: ${response?.statusText}`);
    },

    async post(url, data = null, options = {}) {
        const requestOptions = {
            method: 'POST',
            ...options
        };

        if (data) {
            requestOptions.body = JSON.stringify(data);
        }

        const response = await window.authManager.apiCall(url, requestOptions);
        if (response && response.ok) {
            const text = await response.text();
            return text ? JSON.parse(text) : {};
        }
        throw new Error(`HTTP ${response?.status}: ${response?.statusText}`);
    },

    async logout() {
        await window.authManager.logout();
    }
};

// Add logout functionality to any logout buttons
document.addEventListener('DOMContentLoaded', function () {
    const logoutButtons = document.querySelectorAll('.logout-btn, #logout-btn, [data-logout]');
    logoutButtons.forEach(button => {
        button.addEventListener('click', async (e) => {
            e.preventDefault();
            await window.authManager.logout();
        });
    });

    // Check if user needs to change default password
    checkForDefaultPassword();
});

// Check if default password needs to be changed
async function checkForDefaultPassword() {
    // Skip check for login page
    if (window.location.pathname === '/login') {
        return;
    }

    try {
        const response = await window.authManager.apiCall('/rest/auth/status');
        if (response && response.ok) {
            const data = await response.json();
            if (data.mustChangePassword) {
                showPasswordChangeModal();
            }
        }
    } catch (error) {
        console.error('Failed to check password status:', error);
    }
}

// Show mandatory password change modal
function showPasswordChangeModal() {
    const modal = document.createElement('div');
    modal.id = 'password-change-modal';
    modal.style.cssText = `
        position: fixed;
        top: 0;
        left: 0;
        width: 100%;
        height: 100%;
        background-color: rgba(0, 0, 0, 0.8);
        display: flex;
        justify-content: center;
        align-items: center;
        z-index: 10000;
    `;

    modal.innerHTML = `
        <div style="background: white; padding: 30px; border-radius: 10px; max-width: 400px; width: 90%; box-shadow: 0 0 20px rgba(255,68,68,0.3);">
            <h2 style="color: #ff4444; margin-bottom: 20px; text-align: center;">
                <i class="fas fa-exclamation-triangle"></i> 🚨 Password Intervention Required! 🚨
            </h2>
            <p style="margin-bottom: 20px; text-align: center; font-style: italic;">
                Your password is more predictable than a rom-com ending! 🎬<br>
                Time to upgrade from your terrible password to something your cat can't guess! 🐱
            </p>
            <form id="password-change-form">
                <div style="margin-bottom: 15px;">
                    <label for="current-pwd" style="display: block; margin-bottom: 5px;">Current Password (aka "The Obvious One"):</label>
                    <input type="password" id="current-pwd" required placeholder="Let me guess... 'admin'?" style="width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box;">
                </div>
                <div style="margin-bottom: 15px;">
                    <label for="new-pwd" style="display: block; margin-bottom: 5px;">New Password (Make it spicy! 🌶️):</label>
                    <input type="password" id="new-pwd" required minlength="8" placeholder="Something your goldfish won't remember" style="width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box;">
                </div>
                <div style="margin-bottom: 20px;">
                    <label for="confirm-pwd" style="display: block; margin-bottom: 5px;">Confirm New Password (Yes, again! 🙄):</label>
                    <input type="password" id="confirm-pwd" required placeholder="Copy-paste is your friend!" style="width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box;">
                </div>
                <div id="pwd-error" style="color: red; margin-bottom: 15px; text-align: center; display: none; font-weight: bold;"></div>
                <button type="submit" style="width: 100%; padding: 10px; background: #28a745; color: white; border: none; border-radius: 4px; cursor: pointer; font-weight: bold;">
                    🎯 Upgrade My Digital Fort Knox!
                </button>
            </form>
            <p style="margin-top: 15px; text-align: center; font-size: 12px; color: #666; font-style: italic;">
                Pro tip: Your birthday + "123" is NOT a secure password! 🤦‍♂️
            </p>
        </div>
    `;

    document.body.appendChild(modal);

    // Handle form submission
    document.getElementById('password-change-form').addEventListener('submit', async (e) => {
        e.preventDefault();

        const currentPwd = document.getElementById('current-pwd').value;
        const newPwd = document.getElementById('new-pwd').value;
        const confirmPwd = document.getElementById('confirm-pwd').value;
        const errorDiv = document.getElementById('pwd-error');

        errorDiv.style.display = 'none';

        if (newPwd !== confirmPwd) {
            errorDiv.textContent = 'Oops! Your passwords are having an identity crisis - they don\'t match! 🤪';
            errorDiv.style.display = 'block';
            return;
        }

        if (newPwd.length < 8) {
            errorDiv.textContent = 'Come on, your password needs to hit the gym! At least 8 characters, please! 💪';
            errorDiv.style.display = 'block';
            return;
        } try {
            await window.authAPI.post('/rest/auth/change-password', {
                currentPassword: currentPwd,
                newPassword: newPwd
            });

            // Show success message briefly
            const successDiv = document.createElement('div');
            successDiv.style.cssText = 'background: #d4edda; color: #155724; border: 1px solid #c3e6cb; padding: 10px; border-radius: 4px; margin-bottom: 15px; text-align: center;';
            successDiv.textContent = '🎉 Password upgraded! Time to forget this new one too... Redirecting to login in 3... 2... 1... 🚀';

            const form = document.getElementById('password-change-form');
            form.insertBefore(successDiv, form.firstChild);

            // Clear all auth tokens and redirect to login after password change
            setTimeout(async () => {
                await window.authManager.logout(); // This clears tokens and redirects to login
            }, 2000);
        } catch (error) {
            errorDiv.textContent = 'Password change failed! Your password is apparently as stubborn as you are. Maybe try "elephantMemory" for your current password? 🤷‍♂️';
            errorDiv.style.display = 'block';
        }
    });
}
