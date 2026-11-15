// [CHOSEN, not recovered] Own-use bot sniper tuning (mod/bot-sniper, no upstream issue): four
// aiSettings properties that the retail AI image does not have. Included by AIModule.cpp after the
// generated AIModuleConsole.inc, inside the same anonymous namespace, so they join the same aiList.
// Each property reads with no argument and sets with one, like the generated properties:
//   aiSettings.sniperAccuracyFix 0|1       range-normalized sniper spread (default 1)
//   aiSettings.sniperReferenceRange <m>    the spread reference distance (default 30)
//   aiSettings.sniperAimChest 0|1          sniper aims at a standing soldier's chest (default 1)

#pragma push_macro("BF2_CC_NAME")

#define SNIPER_CC_PROPERTY(className, fnName, T, typeName, member)                                 \
class className : public ::dice::hfe::ConsoleObjectBaseImpl                                        \
{                                                                                                  \
    BF2_CC_HEAD_NAMED(fnName, 1, 0, 1, typeName, true)                                             \
            m_argTypes[0] = typeName;                                                              \
            m_argNeedsConversion[0] = -1;                                                          \
            aiList.push_back(this);                                                                \
        }                                                                                          \
    virtual bool isObjectActive() const { return true; }                                           \
    virtual void executeObjectMethod()                                                             \
    {                                                                                              \
        if (m_argCount == 0)                                                                       \
        {                                                                                          \
            m_hasReturnValue = true;                                                               \
            m_returnValue = sniperTuning().member;                                                 \
        }                                                                                          \
        else if (m_argCount == 1)                                                                  \
        {                                                                                          \
            m_hasReturnValue = false;                                                              \
            sniperTuning().member = BF2_CC_ARG(T, 0);                                              \
        }                                                                                          \
    }                                                                                              \
    virtual void setArgFromString(int index, const std::string& value)                             \
    {                                                                                              \
        if (index == 1)                                                                            \
        {                                                                                          \
            static T arg;                                                                          \
            std::istringstream in(value);                                                          \
            in >> arg;                                                                             \
            m_args[0] = reinterpret_cast<std::string*>(&arg);                                      \
        }                                                                                          \
    }                                                                                              \
    BF2_CC_RETURN_AS_STRING                                                                        \
    BF2_CC_COMMON_HOOKS                                                                            \
private:                                                                                           \
    T m_returnValue;                                                                               \
};

SetConsoleClassDefaults aiSettingsSniperSetConsoleClassDefaults("aiSettings", 2, 0);

#undef BF2_CC_NAME
#define BF2_CC_NAME SniperConsoleAccuracyFix
SNIPER_CC_PROPERTY(SniperConsoleAccuracyFix, "sniperAccuracyFix", bool, "bool", accuracyFix)
BF2_CC_NAME sniperConsoleAccuracyFix;

#undef BF2_CC_NAME
#define BF2_CC_NAME SniperConsoleReferenceRange
SNIPER_CC_PROPERTY(SniperConsoleReferenceRange, "sniperReferenceRange", float, "float", referenceRange)
BF2_CC_NAME sniperConsoleReferenceRange;

#undef BF2_CC_NAME
#define BF2_CC_NAME SniperConsoleAimChest
SNIPER_CC_PROPERTY(SniperConsoleAimChest, "sniperAimChest", bool, "bool", aimChest)
BF2_CC_NAME sniperConsoleAimChest;

#undef SNIPER_CC_PROPERTY
#pragma pop_macro("BF2_CC_NAME")
