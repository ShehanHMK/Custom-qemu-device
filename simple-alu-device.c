/* 1. Adding required libraries. */
#include "qemu/osdep.h"
#include "hw/sysbus.h"         // For system bus integration.
#include "hw/register.h"       // For memory mapped register implementation.
#include "qemu/log.h"          // For logging.
#include "qapi/error.h"        // For error management.
#include "qom/object.h"        // For Qemu object definition.

/* 2. Defining a QOM object. */
#define TYPE_SIMPLE_ALU_DEVICE "amd,simple-alu-pl"

#define SIMPLE_ALU_DEVICE(obj) OBJECT_CHECK(SimpleAluState, (obj), TYPE_SIMPLE_ALU_DEVICE)

/* 3. Adding Memory-mapped registers. */
REG32(REG_CONTROL   ,  0x0000)
REG32(REG_OPERATION ,  0x0004)
REG32(REG_OPERAND_A ,  0x0008)
REG32(REG_OPERAND_B ,  0x000C)
REG32(REG_RESULT    ,  0x0010)
REG32(REG_STATUS    ,  0x0014)

#define R_MAX (R_REG_STATUS + 1)

/* 4. Defining the device internal state struct. */
typedef struct SimpleAluState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    /* Storage for emulated 32-bit registers */
    uint32_t regs[R_MAX];

    /* Register metadata used by QEMU */
    RegisterInfo regs_info[R_MAX];
} SimpleAluState;


enum {
    CTRL_START = 1u << 0,
    CTRL_RESET = 1u << 1,
};

enum {
    STATUS_READY    = 1u << 0,
    STATUS_DONE     = 1u << 1,
    STATUS_ERROR    = 1u << 2,
    STATUS_DIV_ZERO = 1u << 3,
};

typedef enum {
    ALU_ADD = 0,
    ALU_SUB = 1,
    ALU_MUL = 2,
    ALU_DIV = 3,
} AluOperation;


/* pre-write function of reg_control register */
static uint64_t simple_alu_reg_control_pre_write(RegisterInfo *reg, uint64_t val)
{
    SimpleAluState *s = SIMPLE_ALU_DEVICE(reg->opaque);
    uint32_t op = s->regs[R_REG_OPERATION];
    uint32_t b  = s->regs[R_REG_OPERAND_B];

    /* Only START and RESET bits are valid */
    /* bit0 -> start bit and bit0 -> reset bit */
    val &= (CTRL_START | CTRL_RESET);

    /* RESET clears the internal device-visible state */
    if (val & CTRL_RESET) {
        s->regs[R_REG_RESULT] = 0;
        s->regs[R_REG_STATUS] = STATUS_READY;
        return 0;
    }

    /* START request validation */
    if (val & CTRL_START) {
        /* Invalid opcode */
        if (op > ALU_DIV) {
            s->regs[R_REG_STATUS] = STATUS_ERROR;
            val &= ~CTRL_START;
        }

        /* Avoid division by zero */
        if (op == ALU_DIV && b == 0) {
            s->regs[R_REG_STATUS] = STATUS_ERROR | STATUS_DIV_ZERO;
            val &= ~CTRL_START;
        }
    }

    return val;
}


/* post-write function of reg_control register */
static void simple_alu_reg_control_post_write(RegisterInfo *reg, uint64_t val)
{
    SimpleAluState *s = SIMPLE_ALU_DEVICE(reg->opaque);
    uint32_t op = s->regs[R_REG_OPERATION];
    uint32_t a  = s->regs[R_REG_OPERAND_A];
    uint32_t b  = s->regs[R_REG_OPERAND_B];
    uint32_t result = 0;

    if (!(val & CTRL_START)) {
        return;
    }

    /* Clear previous completion/error state before execution */
    s->regs[R_REG_STATUS] &= ~(STATUS_DONE | STATUS_ERROR | STATUS_DIV_ZERO);

    switch (op) {
    case ALU_ADD:
        result = a + b;
        break;
    case ALU_SUB:
        result = a - b;
        break;
    case ALU_MUL:
        result = a * b;
        break;
    case ALU_DIV:
        result = a / b;
        break;
    default:
        s->regs[R_REG_STATUS] = STATUS_ERROR;
        return;
    }

    s->regs[R_REG_RESULT] = result;
    s->regs[R_REG_STATUS] = STATUS_READY | STATUS_DONE;

    /* START behaves like a pulse, not a latched bit */
    s->regs[R_REG_CONTROL] &= ~CTRL_START;
}



/* 5. Add RegisterAccessInfo with corresponding pre-write, post-write functions. */
/* Register descriptions */
static RegisterAccessInfo simple_alu_regs_info[] = {
    { .name = "REG_CONTROL",   .addr = A_REG_CONTROL,
      .pre_write = simple_alu_reg_control_pre_write,
      .post_write = simple_alu_reg_control_post_write },

    { .name = "REG_OPERATION", .addr = A_REG_OPERATION },
    { .name = "REG_OPERAND_A", .addr = A_REG_OPERAND_A },
    { .name = "REG_OPERAND_B", .addr = A_REG_OPERAND_B },
    { .name = "REG_RESULT",    .addr = A_REG_RESULT    },
    { .name = "REG_STATUS",    .addr = A_REG_STATUS    },
};


/* 6. Add standard MMIO handler that delegate to the register API. */
/* Standard MMIO handlers that delegate to the QEMU register API */
static const MemoryRegionOps simple_alu_ops = {
    .read  = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};


/* 7. Add Reset logic. */
static void simple_alu_reset(DeviceState *dev)
{
    SimpleAluState *s = SIMPLE_ALU_DEVICE(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    /* Set initial device state */
    s->regs[R_REG_RESULT] = 0;
    s->regs[R_REG_STATUS] = STATUS_READY;
}


/* 8. Add Initialization logic. */
static void simple_alu_init(Object *obj)
{
    SimpleAluState *s = SIMPLE_ALU_DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;
    const uint64_t region_size = 0x1000;
    const char *name = object_get_typename(obj);

    memory_region_init(&s->iomem, obj, name, region_size);

    reg_array = register_init_block32(DEVICE(obj),
                                      simple_alu_regs_info,
                                      ARRAY_SIZE(simple_alu_regs_info),
                                      s->regs_info,
                                      s->regs,
                                      &simple_alu_ops,
                                      0,
                                      region_size);

    /* Attach register block at offset 0 */
    memory_region_add_subregion(&s->iomem, 0x0, &reg_array->mem);

    /* Expose MMIO region through sysbus */
    sysbus_init_mmio(sbd, &s->iomem);
}


/* 9. Add Device class initialization. */
static void simple_alu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = simple_alu_reset;
}


/* 10. Add Type information. */
static const TypeInfo simple_alu_info = {
    .name          = TYPE_SIMPLE_ALU_DEVICE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SimpleAluState),
    .instance_init = simple_alu_init,
    .class_init    = simple_alu_class_init,
};


/* 11. Register the custom type. */
static void simple_alu_register_types(void)
{
    type_register_static(&simple_alu_info);
}

type_init(simple_alu_register_types)


