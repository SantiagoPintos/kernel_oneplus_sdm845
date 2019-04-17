ifdef CONFIG_OEM_DEBUG_SUPPORT
obj-y += sysrq_x.o
endif
#ifdef VENDOR_EDIT
ifdef CONFIG_OEM_DEBUG_SUPPORT
# oem trace
obj-$(CONFIG_OEM_TRACE_SUPPORT) += oem_trace.o
obj-y += boot_mode.o
endif
#endif /* VENDOR_EDIT */

obj-y += coretech/
