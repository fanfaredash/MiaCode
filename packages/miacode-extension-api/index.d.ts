export interface ExtensionContext {
  extensionPath: string;
  subscriptions: Array<{ dispose(): void }>;
  log(message: string): ApiResult<unknown>;
}

export interface ApiResult<T = unknown> {
  ok: boolean;
  value?: T;
  error?: string;
}

export interface ActiveDocumentSnapshot {
  uri: string;
  languageId: "simai" | string;
  text: string;
  activeDifficultyId: number;
  dirty: boolean;
}

export interface Disposable {
  dispose(): void;
}

export type ApiRisk = "low" | "medium" | "high" | "extreme" | "blocked";
export type ApiStatus = "implemented" | "partial" | "planned" | "blocked";

export interface ApiDescriptor {
  id: string;
  method: string;
  permission?: string;
  risk: ApiRisk;
  status: ApiStatus;
  description: string;
}

export interface ApiRequest {
  id: string;
  reason?: string;
  required?: boolean;
  fallback?: string;
  proposedSignature?: string;
}

export interface MiaCodeApi {
  api: {
    list(): ApiResult<ApiDescriptor[]>;
    has(id: string): ApiResult<boolean>;
    describe(id: string): ApiResult<ApiDescriptor>;
    describeNamespace(namespaceId: string): ApiResult<ApiDescriptor[]>;
    call<T = unknown>(id: string, params?: Record<string, unknown>): ApiResult<T>;
    invoke<T = unknown>(method: string, params?: Record<string, unknown>): ApiResult<T>;
    request(request: ApiRequest): ApiResult;
  };
  app: {
    getInfo(): ApiResult<Record<string, unknown>>;
    openPreferences(): ApiResult;
    reloadExtensions(): ApiResult;
  };
  commands: {
    registerCommand(command: string, callback: () => void): Disposable;
    executeCommand(command: string, args?: unknown): ApiResult;
    getCommands(): ApiResult<Array<Record<string, unknown>>>;
  };
  window: {
    showInformationMessage(message: string): ApiResult;
    showWarningMessage(message: string): ApiResult;
    showErrorMessage(message: string): ApiResult;
    showInputBox(options?: Record<string, unknown>): ApiResult<string>;
    showQuickPick(items: string[], options?: Record<string, unknown>): ApiResult<string>;
    createStatusBarItem(options: { text: string; timeoutMs?: number }): ApiResult;
  };
  workspace: {
    getActiveDocument(): ActiveDocumentSnapshot;
    applyDocumentEdit(edit: { text: string }): ApiResult;
    getRecentFiles(): ApiResult<string[]>;
    getProjectData(key?: string): ApiResult<unknown>;
    setProjectData(key: string, value: unknown): ApiResult;
    scanChartFolders(rootPath: string): ApiResult<string[]>;
    onDidOpenDocument(callback: (event: Record<string, unknown>) => void): Disposable;
    onDidSaveDocument(callback: (event: Record<string, unknown>) => void): Disposable;
    getChartMetadata(): ApiResult<Record<string, unknown>>;
    updateChartMetadata(patch: Record<string, unknown>): ApiResult;
    getChartFolder(): ApiResult<string>;
    getMediaFiles(): ApiResult<string[]>;
    save(): ApiResult;
    saveAs(path: string): ApiResult;
  };
  document: {
    getDifficulties(): ApiResult<Array<Record<string, unknown>>>;
    getActiveDifficulty(): ApiResult<Record<string, unknown>>;
    setActiveDifficulty(id: number): ApiResult;
    replaceActiveDifficultyText(text: string): ApiResult;
    getParsedNoteMarkers(): ApiResult<Array<Record<string, unknown>>>;
    getTimingMetadata(): ApiResult<Record<string, unknown>>;
    applyTextEdits(edits: Array<{ start: number; end: number; text: string }>): ApiResult;
    format(): ApiResult;
    createDifficulty(options: Record<string, unknown>): ApiResult<Record<string, unknown>>;
    deleteDifficulty(id: number): ApiResult;
    renameDifficulty(id: number, label: string): ApiResult;
    onDidChangeText(callback: (event: Record<string, unknown>) => void): Disposable;
  };
  editor: {
    getSelection(): ApiResult<Record<string, number>>;
    getCursor(): ApiResult<Record<string, number>>;
    insertText(text: string): ApiResult;
    replaceSelection(text: string): ApiResult;
    setSelection(range: { start: number; end: number }): ApiResult;
    addDecoration(range: Record<string, unknown>, options?: Record<string, unknown>): ApiResult;
    clearDecorations(ownerId?: string): ApiResult;
    getLine(line: number): ApiResult<Record<string, unknown>>;
    getCurrentLine(): ApiResult<Record<string, unknown>>;
    getCurrentToken(): ApiResult<Record<string, unknown>>;
    replaceRange(range: Record<string, unknown>, text: string): ApiResult;
    showHover(range: Record<string, unknown>, markdown: string): ApiResult;
    addGutterIcon(options: Record<string, unknown>): ApiResult;
    clearGutterIcons(ownerId?: string): ApiResult;
    fold(range: Record<string, unknown>): ApiResult;
    unfold(range: Record<string, unknown>): ApiResult;
    registerHoverProvider(provider: Record<string, unknown>): Disposable;
    registerCompletionProvider(provider: Record<string, unknown>): Disposable;
    registerCodeActionProvider(provider: Record<string, unknown>): Disposable;
    onDidChangeSelection(callback: (event: Record<string, unknown>) => void): Disposable;
  };
  validation: {
    run(): ApiResult;
    getLastResult(): ApiResult<Record<string, unknown>>;
    addDiagnostics(ownerId: string, diagnostics: Array<Record<string, unknown>>): ApiResult;
    clearDiagnostics(ownerId?: string): ApiResult;
  };
  diagnostics: {
    validateDocument(): ApiResult;
  };
  analysis: {
    runMuriAnalysis(): ApiResult;
    getLastMuriResult(): ApiResult<Record<string, unknown>>;
  };
  timeline: {
    getSnapshot(): ApiResult<Record<string, unknown>>;
    getCurrentSecond(): ApiResult<number>;
    seek(second: number): ApiResult;
    addMarker(marker: Record<string, unknown>): ApiResult;
    clearMarkers(ownerId?: string): ApiResult;
    addBand(band: Record<string, unknown>): ApiResult;
    addVerticalLine(line: Record<string, unknown>): ApiResult;
    clearVisuals(ownerId?: string): ApiResult;
    registerMarkerClickCommand(command: string): ApiResult;
    onDidSeek(callback: (event: Record<string, unknown>) => void): Disposable;
  };
  preview: {
    play(): ApiResult;
    pause(): ApiResult;
    stop(): ApiResult;
    seek(second: number): ApiResult;
    getState(): ApiResult<Record<string, unknown>>;
    setSpeed(value: number): ApiResult;
    addOverlay(overlay: Record<string, unknown>): ApiResult;
    clearOverlays(ownerId?: string): ApiResult;
    onDidChangeState(callback: (event: Record<string, unknown>) => void): Disposable;
    onFrame(callback: (event: Record<string, unknown>) => void): Disposable;
  };
  export: {
    getPresets(): ApiResult<Array<Record<string, unknown>>>;
    registerPreset(preset: Record<string, unknown>): ApiResult;
    startVideoExport(options: Record<string, unknown>): ApiResult;
    startCoverExport(options: Record<string, unknown>): ApiResult;
    registerBeforeExportHook(hook: Record<string, unknown>): Disposable;
    registerAfterExportHook(hook: Record<string, unknown>): Disposable;
    registerCoverTemplate(templateSpec: Record<string, unknown>): Disposable;
    registerBatchJobProvider(provider: Record<string, unknown>): Disposable;
  };
  resources: {
    getMediaInfo(): ApiResult<Record<string, unknown>>;
    getAssetPath(id: string): ApiResult<string>;
    setAssetPath(id: string, path: string): ApiResult;
  };
  fs: {
    readText(path: string): ApiResult<string>;
    writeText(path: string, text: string): ApiResult;
    exists(path: string): ApiResult<boolean>;
    listDir(path: string): ApiResult<Array<Record<string, unknown>>>;
  };
  net: {
    fetch(url: string, options?: Record<string, unknown>): ApiResult<{ status: number; text: string }>;
    download(url: string, targetPath: string): ApiResult<{ status: number; path: string }>;
  };
  settings: {
    get(key: string): ApiResult<unknown>;
    set(key: string, value: unknown): ApiResult;
  };
  extensions: {
    all(): ApiResult<Array<Record<string, unknown>>>;
    get(id: string): ApiResult<Record<string, unknown>>;
    enable(id: string): ApiResult;
    disable(id: string): ApiResult;
    installFromFolder(path: string): ApiResult;
    remove(id: string): ApiResult;
  };
  ui: {
    registerSidebarView(view: Record<string, unknown>): Disposable;
    registerBottomTabView(view: Record<string, unknown>): Disposable;
    registerPreferencesPage(page: Record<string, unknown>): Disposable;
    registerToolbarButton(button: Record<string, unknown>): Disposable;
    getContributions(): ApiResult<Array<Record<string, unknown>>>;
  };
  tasks: {
    withProgress(options: Record<string, unknown>, callback?: unknown): ApiResult<Record<string, unknown>>;
    registerTask(task: Record<string, unknown>): Disposable;
    reportProgress(taskId: string, percent: number, message?: string): ApiResult;
  };
  logs: {
    append(channel: string, message: string): ApiResult;
    getPath(channel?: string): ApiResult<string>;
    open(channel?: string): ApiResult;
  };
}

export function activate(context: ExtensionContext): void;
export function deactivate(): void;

declare global {
  const miacode: MiaCodeApi;
}
