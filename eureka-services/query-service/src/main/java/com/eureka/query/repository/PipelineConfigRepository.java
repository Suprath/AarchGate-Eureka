package com.eureka.query.repository;

import com.eureka.query.model.PipelineConfig;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

@Repository
public interface PipelineConfigRepository extends JpaRepository<PipelineConfig, Long> {
}
