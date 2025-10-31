import requests
import json
import os

class ApiClient:

    def __init__(self, instance_url):
        if not instance_url.endswith('/'):
            instance_url += '/'
        self.base_url = instance_url + 'api.php'
        self.api_version = 10
        self.session = requests.Session()
        self.user_token = None
        print(f"API Client initialized for URL: {self.base_url}")

    def _make_request(self, method, params=None, data=None):
        if params is None:
            params = {}
        params['method'] = method
        params['v'] = self.api_version
        headers = {}
        if self.user_token:
            params['user'] = self.user_token
            headers['X-MPGRAM-USER'] = self.user_token
            
        try:
            response = self.session.post(self.base_url, params=params, data=data, headers=headers, timeout=30)
            response.raise_for_status()
            if 'image/' in response.headers.get('Content-Type', ''):
                return response.content
            return response.json()
        except requests.exceptions.HTTPError as e:
            print(f"\n--- HTTP Error ---\nError: {e}\nResponse Body: {e.response.text}\n------------------\n")
            return None
        except requests.exceptions.RequestException as e:
            print(f"\n--- Request Error ---\nAn error occurred: {e}\n---------------------\n")
            return None
        except json.JSONDecodeError:
            print(f"\n--- JSON Decode Error ---\nFailed to decode response: {response.text}\n-------------------------\n")
            return None

    def set_user_token(self, token):
        self.user_token = token

    def phone_login_start(self, phone):
        return self._make_request('phoneLogin', params={'phone': phone})

    def get_captcha_image(self, captcha_id):
        return self._make_request('getCaptchaImg', params={'captcha_id': captcha_id})

    def phone_login_with_captcha(self, phone, captcha_id, captcha_key):
        return self._make_request('phoneLogin', params={'phone': phone, 'captcha_id': captcha_id, 'captcha_key': captcha_key})

    def complete_phone_login(self, code):
        return self._make_request('completePhoneLogin', params={'code': code})

    def complete_2fa_login(self, password):
        return self._make_request('complete2faLogin', params={'password': password})

def setup_and_login():
    """
    Guides the user through setting up WiFi, server, and logging into Telegram
    to generate a complete config file for the M5Cardputer.
    """
    print("--- M5Cardputer Full Config and Login Assistant ---")
    print("This script will configure WiFi and log you into Telegram to generate a complete session file.\n")
    
    try:
        ssid = input("Enter your WiFi SSID: ")
        password = input("Enter your WiFi Password: ")
        server_url = input("Enter the FULL instance URL of your server (e.g., http://mp.nnchan.ru/): ")
        if not server_url.startswith(('http://', 'https://')):
            server_url = 'http://' + server_url
    except KeyboardInterrupt:
        print("\n\nOperation cancelled. Exiting.")
        return

    server_ip = server_url.split('//')[1].split('/')[0]

    client = ApiClient(server_url)
    final_response = None

    try:
        print("\n--- Telegram Login ---")
        phone = input("Enter your phone number (with country code, e.g., +15551234567): ")

        response = client.phone_login_start(phone)
        if not response: return

        if response.get('res') == 'need_captcha':
            captcha_id = response.get('captcha_id')
            print(f"Captcha is required. Captcha ID: {captcha_id}")
            
            img_data = client.get_captcha_image(captcha_id)
            if img_data:
                with open("captcha.png", "wb") as f:
                    f.write(img_data)
                print("Captcha image saved as 'captcha.png'. Please open it and enter the text below.")
                captcha_key = input("Enter captcha text: ")
                
                response = client.phone_login_with_captcha(phone, captcha_id, captcha_key)
                if not response or response.get('res') == 'wrong_captcha':
                    print("❌ Wrong captcha or error. Please restart the script.")
                    return

        if 'user' not in response:
            print("❌ Login failed. Could not get a user token from the server.")
            print("Server response:", json.dumps(response, indent=2))
            return

        client.set_user_token(response.get('user'))
        print("✅ Phone number accepted. A code has been sent to your Telegram account.")

        code = input("Enter the verification code from Telegram: ")
        final_response = client.complete_phone_login(code)
        if not final_response: return

        if final_response.get('res') == 'password':
            password_2fa = input("2FA Password required. Please enter it: ")
            final_response = client.complete_2fa_login(password_2fa)
            if not final_response: return
        
    except KeyboardInterrupt:
        print("\n\nOperation cancelled. Exiting.")
        return
        
    if final_response and final_response.get('res') == 1:
        final_user_token = client.user_token
        print("\n✅ Login successful! Final user token has been captured.")

        config_data = {
            "ssid": ssid,
            "password": password,
            "server_ip": server_ip,
            "phone": phone,
            "user_token": final_user_token
        }
        filename = "sushigram.json"

        try:
            with open(filename, 'w') as f:
                json.dump(config_data, f, indent=4)
            
            print("\n" + "="*50)
            print(f"✅ Success! The configuration file '{filename}' has been created with your session token.")
            print("="*50)
            print("\nNext Steps:")
            print(f"1. Copy the '{filename}' file to the main (root) directory of your M5Cardputer's SD card.")
            print(f"2. Insert the SD card and power on the device.")
            print("3. The Cardputer will now be logged in and ready to use!")
            print("="*50 + "\n")

        except IOError as e:
            print(f"\n[ERROR] Could not write to file '{filename}'. Reason: {e}")
    else:
        # This will now correctly show failure messages.
        print("\n❌ Login failed at the final step.")
        print("Server response:", json.dumps(final_response, indent=2))


if __name__ == "__main__":
    setup_and_login()
