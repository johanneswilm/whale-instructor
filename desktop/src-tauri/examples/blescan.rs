// SPDX-FileCopyrightText: Johannes Wilm
// SPDX-License-Identifier: GPL-3.0-or-later
//! Temporary diagnostic: run the blec plugin's exact scan path without the
//! Tauri UI to isolate the crash reported from the webview flow.
use tauri_plugin_blec::get_handler;
use tauri_plugin_blec::models::ScanFilter;

fn main() {
    let _plugin = tauri_plugin_blec::init();
    let rt = tokio::runtime::Runtime::new().unwrap();
    rt.block_on(async {
        let handler = get_handler().unwrap();
        let (tx, mut rx) = tokio::sync::mpsc::channel(1);
        let scan = tokio::spawn(async move {
            let r = handler.discover(Some(tx), 6000, ScanFilter::None, false).await;
            eprintln!("discover returned: {r:?}");
        });
        while let Some(devs) = rx.recv().await {
            for d in &devs {
                println!("{} name={:?} rssi={:?} services={:?}",
                         d.address, d.name, d.rssi, d.services);
            }
        }
        scan.await.unwrap();
        println!("scan task done");
    });
}
