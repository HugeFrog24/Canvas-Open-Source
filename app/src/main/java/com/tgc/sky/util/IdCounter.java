package com.tgc.sky.util;

import java.lang.Number;

public class IdCounter<Id extends Number> {
    private final Id kInvalidId;
    private Id m_id;
    private Id m_next;

    public IdCounter(Id id) {
        this.kInvalidId = id;
        this.m_id = id;
        this.m_next = id;
    }

    public Id TryActivate() {
        if (!this.m_id.equals(this.kInvalidId)) {
            return this.kInvalidId;
        }
        Id id = m_Increment(this.m_next);
        this.m_next = id;
        this.m_id = id;
        return id;
    }

    public Id GetActiveId() {
        return this.m_id;
    }

    public boolean IsActiveId(Id id) {
        return !id.equals(this.kInvalidId) && id.equals(this.m_id);
    }

    public void ClearId() {
        this.m_id = this.kInvalidId;
    }

    @SuppressWarnings("unchecked")
    private Id m_Increment(Id id) {
        if (id instanceof Integer) {
            return (Id) Integer.valueOf(id.intValue() + 1);
        }
        if (id instanceof Long) {
            return (Id) Long.valueOf(id.longValue() + 1);
        }
        if (id instanceof Short) {
            return (Id) Short.valueOf((short) (id.shortValue() + 1));
        }
        if (id instanceof Byte) {
            return (Id) Byte.valueOf((byte) (id.byteValue() + 1));
        }
        throw new UnsupportedOperationException("Unsupported Id type");
    }
}
