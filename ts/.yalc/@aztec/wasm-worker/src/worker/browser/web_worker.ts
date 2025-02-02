import { WasmModule } from '../../wasm/wasm_module.js';
import { createDispatchProxy, DispatchMsg, TransportClient, WorkerConnector } from '../../transport/index.js';
import { WasmWorker } from '../wasm_worker.js';

/**
 *
 */
export async function createWebWorker(url: string, initialMem?: number, maxMem?: number): Promise<WasmWorker> {
  const worker = new Worker(url);
  const transportConnect = new WorkerConnector(worker);
  const transportClient = new TransportClient<DispatchMsg>(transportConnect);
  await transportClient.open();
  const remoteModule = createDispatchProxy(WasmModule, transportClient) as WasmWorker;
  remoteModule.destroyWorker = async () => {
    await transportClient.request({ fn: '__destroyWorker__', args: [] });
    transportClient.close();
  };
  await remoteModule.init(initialMem, maxMem);
  return remoteModule;
}
