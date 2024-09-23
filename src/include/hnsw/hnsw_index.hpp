#pragma once

#include "duckdb/execution/index/bound_index.hpp"
#include "duckdb/execution/index/index_pointer.hpp"
#include "duckdb/execution/index/fixed_size_allocator.hpp"
#include "duckdb/common/array.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/common/unordered_map.hpp"

#include "usearch/duckdb_usearch.hpp"

namespace duckdb {

class StorageLock;

struct HNSWIndexStats {
	idx_t max_level;
	idx_t count;
	idx_t capacity;
	idx_t approx_size;
	vector<unum::usearch::index_dense_gt<row_t>::stats_t> level_stats;
};

class HNSWIndex : public BoundIndex {
public:
	// The type name of the HNSWIndex
	static constexpr const char *TYPE_NAME = "HNSW";

public:
	HNSWIndex(const string &name, IndexConstraintType index_constraint_type, const vector<column_t> &column_ids,
	          TableIOManager &table_io_manager, const vector<unique_ptr<Expression>> &unbound_expressions,
	          AttachedDatabase &db, const case_insensitive_map_t<Value> &options,
	          const IndexStorageInfo &info = IndexStorageInfo(), idx_t estimated_cardinality = 0);

	//! The actual usearch index
	unum::usearch::index_dense_gt<row_t> index;

	//! Block pointer to the root of the index
	IndexPointer root_block_ptr;

	//! The allocator used to persist linked blocks
	unique_ptr<FixedSizeAllocator> linked_block_allocator;

	unique_ptr<IndexScanState> InitializeScan(float *query_vector, idx_t limit, ClientContext &context);
	idx_t Scan(IndexScanState &state, Vector &result);
	idx_t GetVectorSize() const;
	bool MatchesDistanceFunction(const string &distance_function_name) const;
	string GetMetric() const;

	void Construct(DataChunk &input, Vector &row_ids, idx_t thread_idx);
	void PersistToDisk();
	void Compact();

	unique_ptr<HNSWIndexStats> GetStats();

	static const case_insensitive_map_t<unum::usearch::metric_kind_t> METRIC_KIND_MAP;
	static const unordered_map<uint8_t, unum::usearch::scalar_kind_t> SCALAR_KIND_MAP;

public:
	//! Called when data is appended to the index. The lock obtained from InitializeLock must be held
	ErrorData Append(IndexLock &lock, DataChunk &entries, Vector &row_identifiers) override;
	//! Verify that data can be appended to the index without a constraint violation
	void VerifyAppend(DataChunk &chunk) override;
	//! Verify that data can be appended to the index without a constraint violation using the conflict manager
	void VerifyAppend(DataChunk &chunk, ConflictManager &conflict_manager) override;
	//! Deletes all data from the index. The lock obtained from InitializeLock must be held
	void CommitDrop(IndexLock &index_lock) override;
	//! Delete a chunk of entries from the index. The lock obtained from InitializeLock must be held
	void Delete(IndexLock &lock, DataChunk &entries, Vector &row_identifiers) override;
	//! Insert a chunk of entries into the index
	ErrorData Insert(IndexLock &lock, DataChunk &data, Vector &row_ids) override;

	IndexStorageInfo GetStorageInfo(const case_insensitive_map_t<Value> &options, const bool to_wal) override;
	idx_t GetInMemorySize(IndexLock &state) override;

	//! Merge another index into this index. The lock obtained from InitializeLock must be held, and the other
	//! index must also be locked during the merge
	bool MergeIndexes(IndexLock &state, BoundIndex &other_index) override;

	//! Traverses an HNSWIndex and vacuums the qualifying nodes. The lock obtained from InitializeLock must be held
	void Vacuum(IndexLock &state) override;

	//! Performs constraint checking for a chunk of input data
	void CheckConstraintsForChunk(DataChunk &input, ConflictManager &conflict_manager) override;

	//! Returns the string representation of the HNSWIndex, or only traverses and verifies the index.
	string VerifyAndToString(IndexLock &state, const bool only_verify) override;
	//! Ensures that the node allocation counts match the node counts.
	void VerifyAllocations(IndexLock &state) override;

	string GetConstraintViolationMessage(VerifyExistenceType verify_type, idx_t failed_index,
	                                     DataChunk &input) override {
		return "Constraint violation in HNSW index";
	}

	void SetDirty() {
		is_dirty = true;
	}
	void SyncSize() {
		index_size = index.size();
	}

	bool CanRewriteIndexExpression(LogicalGet &get, Expression &column_ref) const;

private:
	bool is_dirty = false;
	StorageLock rwlock;
	atomic<idx_t> index_size = {0};
};

} // namespace duckdb