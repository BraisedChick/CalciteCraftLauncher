package com.calcite.account;

public class Account {
    private String name;
    private String type; // "offline" or "premium"
    private String uuid;

    public Account(String name, String type, String uuid) {
        this.name = name;
        this.type = type;
        this.uuid = uuid;
    }

    public String getName() { return name; }
    public String getType() { return type; }
    public String getUuid() { return uuid; }
    public void setName(String name) { this.name = name; }
    public void setType(String type) { this.type = type; }
    public void setUuid(String uuid) { this.uuid = uuid; }

    public String getTypeDisplay() {
        return "offline".equals(type) ? "离线账号" : "正版账号";
    }
}
