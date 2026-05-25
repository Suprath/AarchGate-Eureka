package com.eureka.query.service;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import jakarta.annotation.PostConstruct;
import java.io.*;
import java.net.URL;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

@Service
public class ScratchStorageService {

    @Value("${aarchgate.scratch.dir:scratch}")
    private String scratchDirProperty;

    @Value("${aarchgate.scratch.max-size-gb:5}")
    private double maxCacheSizeGb;

    private Path scratchDirPath;
    
    // Simple metadata tracking for cache eviction (filePath -> lastAccessTime)
    private final Map<String, Long> accessTimes = new ConcurrentHashMap<>();

    private Path getWorkspaceRoot() {
        Path current = Paths.get(System.getProperty("user.dir"));
        Path workspaceRoot = current;
        while (current != null) {
            if (Files.exists(current.resolve("CMakeLists.txt")) || Files.exists(current.resolve(".git"))) {
                workspaceRoot = current;
                break;
            }
            current = current.getParent();
        }
        return workspaceRoot;
    }

    @PostConstruct
    public void init() throws IOException {
        // Resolve absolute path inside workspace if relative
        Path path = Paths.get(scratchDirProperty);
        if (!path.isAbsolute()) {
            path = getWorkspaceRoot().resolve(path);
        }
        this.scratchDirPath = path;
        
        if (!Files.exists(scratchDirPath)) {
            Files.createDirectories(scratchDirPath);
        }
        System.out.println("[Spring Boot] Scratch Storage Directory initialized at: " + scratchDirPath.toAbsolutePath());
    }

    public String resolveAndFetch(String filePath) throws IOException {
        if (filePath == null || filePath.isEmpty()) {
            throw new IllegalArgumentException("File path cannot be empty");
        }

        // 1. Local path: return absolute path directly
        if (!filePath.startsWith("s3://") && !filePath.startsWith("http://") && !filePath.startsWith("https://")) {
            Path p = Paths.get(filePath);
            if (!p.isAbsolute()) {
                p = getWorkspaceRoot().resolve(p);
            }
            return p.toAbsolutePath().toString();
        }

        // 2. Cloud Storage URI (S3 or HTTP)
        String cacheKey = generateCacheKey(filePath);
        Path cachedFile = scratchDirPath.resolve(cacheKey);

        // Update access time for LRU tracking
        accessTimes.put(cacheKey, System.currentTimeMillis());

        if (Files.exists(cachedFile)) {
            System.out.println("[Scratch Storage] Cache HIT for: " + filePath + " -> " + cachedFile.toAbsolutePath());
            return cachedFile.toAbsolutePath().toString();
        }

        // Cache MISS: Download or simulate download
        System.out.println("[Scratch Storage] Cache MISS for: " + filePath + ". Downloading to: " + cachedFile.toAbsolutePath());
        
        // Ensure cache capacity before downloading
        evictIfNecessary(cachedFile);

        if (filePath.startsWith("s3://")) {
            simulateS3Download(filePath, cachedFile);
        } else {
            downloadHttpFile(filePath, cachedFile);
        }

        return cachedFile.toAbsolutePath().toString();
    }

    private String generateCacheKey(String urlOrUri) {
        String clean = urlOrUri.replaceAll("[^a-zA-Z0-9.-]", "_");
        if (clean.length() > 100) {
            clean = clean.substring(clean.length() - 100);
        }
        return clean;
    }

    private void simulateS3Download(String s3Uri, Path targetPath) throws IOException {
        String filename = s3Uri.substring(s3Uri.lastIndexOf('/') + 1);
        
        // Look for the file locally in the workspace root to copy (simulating a download)
        Path localSource = getWorkspaceRoot().resolve(filename);

        if (Files.exists(localSource)) {
            System.out.println("[Scratch Storage] Simulating S3 download by copying local file: " + localSource.toAbsolutePath());
            Files.copy(localSource, targetPath, StandardCopyOption.REPLACE_EXISTING);
        } else {
            System.out.println("[Scratch Storage] Local simulation source not found: " + filename + ". Creating mock file.");
            try (PrintWriter writer = new PrintWriter(Files.newBufferedWriter(targetPath))) {
                writer.println("{\"status\":200,\"latency\":100}");
                writer.println("{\"status\":500,\"latency\":300}");
            }
        }
    }

    private void downloadHttpFile(String urlStr, Path targetPath) throws IOException {
        URL url = new URL(urlStr);
        try (InputStream in = url.openStream()) {
            Files.copy(in, targetPath, StandardCopyOption.REPLACE_EXISTING);
        }
    }

    private void evictIfNecessary(Path newFile) {
        long currentSize = getCacheSizeInBytes();
        long limitBytes = (long) (maxCacheSizeGb * 1024 * 1024 * 1024);

        if (currentSize < limitBytes) {
            return;
        }

        System.out.println("[Scratch Storage] Cache size limit reached. Performing eviction...");
        
        List<Map.Entry<String, Long>> sortedAccess = new ArrayList<>(accessTimes.entrySet());
        sortedAccess.sort(Map.Entry.comparingByValue());

        for (Map.Entry<String, Long> entry : sortedAccess) {
            Path toDelete = scratchDirPath.resolve(entry.getKey());
            try {
                if (Files.exists(toDelete)) {
                    long size = Files.size(toDelete);
                    Files.delete(toDelete);
                    accessTimes.remove(entry.getKey());
                    System.out.println("[Scratch Storage] Evicted: " + toDelete.getFileName() + " (" + (size / (1024 * 1024)) + " MB)");
                    
                    currentSize -= size;
                    if (currentSize < limitBytes) {
                        break;
                    }
                }
            } catch (IOException e) {
                System.err.println("[Scratch Storage] Failed to delete: " + toDelete + " - " + e.getMessage());
            }
        }
    }

    private long getCacheSizeInBytes() {
        try (var stream = Files.walk(scratchDirPath)) {
            return stream.filter(Files::isRegularFile)
                         .mapToLong(p -> {
                             try {
                                 return Files.size(p);
                             } catch (IOException e) {
                                 return 0L;
                             }
                         })
                         .sum();
        } catch (IOException e) {
            return 0L;
        }
    }
}
