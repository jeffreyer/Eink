#pragma once

#include <Arduino.h>
#include <Preferences.h>

/**
 * BLE设备安全管理类
 *
 * 功能：
 * 1. 生成并存储设备密码（6位数字）
 * 2. 验证用户输入的密码
 * 3. 管理设备绑定状态
 *
 * 安全方案：BLE绑定 + 业务密码 + 绑定状态管理
 * - BLE绑定：提供通讯加密（AES-128-CCM）
 * - 业务密码：提供访问控制（防止未授权用户连接）
 * - 绑定状态：未绑定时可读取密码，已绑定时拒绝读取
 */
class DeviceSecurity {
private:
    Preferences prefs;
    String devicePassword;
    bool isAuthenticated;
    unsigned long authTime;
    bool isBound; // 设备绑定状态

    static const unsigned long AUTH_TIMEOUT_MS = 30 * 60 * 1000; // 30分钟

public:
    DeviceSecurity() : isAuthenticated(false), authTime(0), isBound(false) {}

    /**
     * 初始化安全模块
     * 从NVS加载或生成新密码，加载绑定状态
     */
    void init() {
        prefs.begin("ble_sec", false);

        // 尝试从NVS加载密码
        devicePassword = prefs.getString("password", "");

        // 如果没有密码，生成新密码
        if (devicePassword.length() == 0) {
            devicePassword = generatePassword();
            prefs.putString("password", devicePassword);
        }

        // 加载绑定状态
        isBound = prefs.getBool("bound", false);

        prefs.end();
    }

    /**
     * 生成6位数字密码
     */
    String generatePassword() {
        String password = "";
        for (int i = 0; i < 6; i++) {
            password += String(random(0, 10));
        }
        return password;
    }

    /**
     * 获取设备密码（用于显示给用户）
     * 如果已绑定，返回"BOUND"标识
     */
    String getPassword() {
        if (isBound) {
            return "BOUND";
        }
        return devicePassword;
    }

    /**
     * 验证密码
     */
    bool verifyPassword(const String& inputPassword) {
        if (inputPassword == devicePassword) {
            isAuthenticated = true;
            authTime = millis();
            return true;
        } else {
            return false;
        }
    }

    /**
     * 检查是否已认证
     */
    bool checkAuthenticated() {
        if (!isAuthenticated) {
            return false;
        }

        // 检查认证是否过期
        if (millis() - authTime > AUTH_TIMEOUT_MS) {
            isAuthenticated = false;
            return false;
        }

        return true;
    }

    /**
     * 重置认证状态（断开连接时调用）
     */
    void resetAuth() {
        isAuthenticated = false;
        authTime = 0;
        Serial.println("BLE Security: Authentication reset");
    }

    /**
     * 重新生成密码（解绑时调用）
     */
    void regeneratePassword() {
        devicePassword = generatePassword();
        prefs.begin("ble_sec", false);
        prefs.putString("password", devicePassword);
        prefs.end();

        isAuthenticated = false;
        authTime = 0;
    }

    /**
     * 设置绑定状态
     */
    void setBound(bool bound) {
        isBound = bound;
        prefs.begin("ble_sec", false);
        prefs.putBool("bound", bound);
        prefs.end();
    }

    /**
     * 获取绑定状态
     */
    bool getBound() {
        return isBound;
    }

    /**
     * 解绑设备（清除绑定状态并重新生成密码）
     */
    void unbind() {
        setBound(false);
        regeneratePassword();
    }
};
