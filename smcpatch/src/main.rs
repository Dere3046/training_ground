use std::env;
use std::fs;
use std::io::Cursor;
use std::path::Path;

use android_bootimg::parser::BootImage;
use android_bootimg::patcher::BootImagePatchOption;
use android_bootimg::cpio::{Cpio, CpioEntry};
use anyhow::Context;

fn main() -> anyhow::Result<()> {
    let args: Vec<String> = env::args().collect();

    if args.len() < 4 {
        eprintln!("usage: {} <init_boot.img> <module.ko> --wrap <init-wrapper> [output.img]", args[0]);
        std::process::exit(1);
    }

    let boot_path = &args[1];
    let ko_path = &args[2];

    let mut wrapper_path: Option<&String> = None;
    let mut output = "patched_init_boot.img";
    let mut idx = 3;
    while idx < args.len() {
        if args[idx] == "--wrap" {
            idx += 1;
            wrapper_path = args.get(idx);
        } else {
            output = &args[idx];
        }
        idx += 1;
    }

    let wrapper_path = wrapper_path.context("--wrap <init-wrapper> is required")?;

    let boot_data = fs::read(boot_path)
        .with_context(|| format!("read {}", boot_path))?;
    let boot = BootImage::parse(&boot_data)
        .context("parse boot image")?;
    println!("boot image version: {:?}", boot.get_header().get_version());

    let blocks = boot.get_blocks();
    let ramdisk = blocks.get_ramdisk().context("no ramdisk")?;

    let mut rd_buf = Vec::new();
    ramdisk.dump(&mut rd_buf, false).context("decompress ramdisk")?;

    let mut cpio = Cpio::load_from_data(&rd_buf).context("parse cpio")?;
    println!("cpio entries: {}", cpio.entries().len());

    let ko_data = fs::read(ko_path)
        .with_context(|| format!("read {}", ko_path))?;

    let ko_name = Path::new(ko_path)
        .file_name()
        .and_then(|n| n.to_str())
        .context("invalid module filename")?;

    let has_ksu = cpio.exists("kernelsu.ko");

    cpio.rm(ko_name, false);

    if has_ksu {
        cpio.mv("init", "init.ksu")?;
        println!("ksu: init -> init.ksu");
    } else {
        cpio.mv("init", "init.real")?;
        println!("stock: init -> init.real");
    }

    let wrapper_data = fs::read(wrapper_path)
        .with_context(|| format!("read {}", wrapper_path))?;
    cpio.add("init", CpioEntry::regular(0o755, Box::new(wrapper_data)))?;
    println!("init-wrapper as /init");

    cpio.add(ko_name, CpioEntry::regular(0o755, Box::new(ko_data)))?;
    println!("added {}", ko_name);

    let mut new_rd = Vec::new();
    cpio.dump(&mut new_rd).context("dump cpio")?;

    let mut patcher = BootImagePatchOption::new(&boot);
    patcher.replace_ramdisk(Box::new(Cursor::new(new_rd)), false);

    let mut out = Cursor::new(Vec::with_capacity(boot_data.len()));
    patcher.patch(&mut out).context("patch boot image")?;

    let out_path = Path::new(output);
    fs::write(out_path, out.into_inner())
        .with_context(|| format!("write {}", output))?;
    println!("done: {} (ksu={})", output, has_ksu);
    Ok(())
}
