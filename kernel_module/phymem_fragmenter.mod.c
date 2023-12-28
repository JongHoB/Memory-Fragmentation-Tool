#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xf26ece1c, "module_layout" },
	{ 0x4df446c3, "param_ops_int" },
	{ 0x92997ed8, "_printk" },
	{ 0xa92e098c, "__free_pages" },
	{ 0x935eef76, "split_page" },
	{ 0x123b8256, "alloc_pages" },
	{ 0x8da6585d, "__stack_chk_fail" },
	{ 0xb837787b, "node_data" },
	{ 0x7bbccd05, "nr_node_ids" },
	{ 0xa648e561, "__ubsan_handle_shift_out_of_bounds" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "AFA1E684844B850688B7C35");
