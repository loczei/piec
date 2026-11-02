#ifndef _BEHAVIOURENGINE_HPP_
#define _BEHAVIOURENGINE_HPP_

#pragma once

#include <Arduino.h>

using ValueType = std::variant<int32_t, float>;

template<typename T>
concept ValueVariant = std::is_same_v<T, int32_t> || std::is_same_v<T, float>;

class ValueBase {
public:
    virtual ValueBase() = default;
    virtual ~ValueBase() = default;
    virtual ValueType get() = 0;
    virtual void set(ValueType) = 0;
    virtual bool is_user_defined() = 0;
    virtual const char* get_name() = 0;
    virtual const char* get_description() = 0;
}

template<ValueVariant T>
class Value : public ValueBase {
    T value;
    bool user_defined;
    char name[11];
    char description[50];
public:
    Value(T v, bool ud, const char* n, const char* des) : value(v), user_defined(ud) {
        const char* src_name = n ? n : "Error";
        std::strncpy(this->name, src_name, sizeof(this->name) - 1);
        this->name[sizeof(this->name) - 1] = '\0';

        const char* src_des = des ? des : "No description!";
        std::strncpy(this->description, src_des, sizeof(this->description) - 1);
        this->description[sizeof(this->description) - 1] = '\0';
    }
    ~Value() = default;

    ValueType get() override {
        return this->value;
    }

    ValueType set(ValueType v) override {
        this->value = std::get<T>(v);
    }

    bool is_user_defined() override { return this->user_defined; }
    const char* get_name() override { return this->name; }
    const char* get_description() override { return this->description; }
}


using ValueAny = std::variant<Value<int32_t>, Value<float>>;

enum Behaviours : uint8_t {
    EXIT,
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    FLOOR,
    CEIL,
    ROUND,
    MIN,
    MAX,
    SQRT,
    ABS,
};


class BehaviourEngine {
    ValueAny values[100];
    uint8_t bytecode[1000];
public:

    uint8_t get_amount_of_args(Behaviours b) {
        switch (b) {
            case ADD, SUB, MUL, DIV, MOD: return 3;
            case MIN, MAX: return 2;
            case FLOOR, CEIL, ROUND, SQRT, ABS: return 1;
            case EXIT: return 0;
        }
    }

    bool execute() {
        uint16_t i = 0;
        Behaviours b = EXIT;
        while (b = Behaviours(bytecode[i]) != Behaviours::EXIT) {
            uint8_t args = get_amount_of_args(b);
            this->step(b, )
        }
    }

    bool step(Behaviours b, uint8_t a, uint8_t b, uint8_t c) {
        // tu switch wykonywujący
    }
};

#endif
