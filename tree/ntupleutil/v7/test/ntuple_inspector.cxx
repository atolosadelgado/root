#include "ntupleutil_test.hxx"

TEST(RNTupleInspector, Name)
{
   FileRaii fileGuard("test_ntuple_inspector_name.root");
   {
      auto model = RNTupleModel::Create();
      RNTupleWriter::Recreate(std::move(model), "ntuple", fileGuard.GetPath());
   }

   auto ntuple = RNTupleReader::Open("ntuple", fileGuard.GetPath());
   auto inspector = RNTupleInspector::Create(ntuple).Unwrap();
   EXPECT_EQ("ntuple", inspector->GetName());
}

TEST(RNTupleInspector, CompressionSettings)
{
   FileRaii fileGuard("test_ntuple_inspector_size_single_int_field.root");
   {
      auto model = RNTupleModel::Create();
      auto nFldInt = model->MakeField<std::int32_t>("i");

      auto writeOptions = RNTupleWriteOptions();
      writeOptions.SetCompression(505);
      auto ntuple = RNTupleWriter::Recreate(std::move(model), "ntuple", fileGuard.GetPath(), writeOptions);

      *nFldInt = 42;
      ntuple->Fill();
   }

   auto ntuple = RNTupleReader::Open("ntuple", fileGuard.GetPath());

   auto inspector = RNTupleInspector::Create(ntuple).Unwrap();
   EXPECT_EQ(505, inspector->GetCompressionSettings());
}

TEST(RNTupleInspector, SizeUncompressed)
{
   FileRaii fileGuard("test_ntuple_inspector_size_uncompressed.root");
   {
      auto model = RNTupleModel::Create();
      auto nFldInt = model->MakeField<std::int32_t>("i");

      auto writeOptions = RNTupleWriteOptions();
      writeOptions.SetCompression(0);
      auto ntuple = RNTupleWriter::Recreate(std::move(model), "ntuple", fileGuard.GetPath(), writeOptions);

      *nFldInt = 42;
      ntuple->Fill();
   }

   auto ntuple = RNTupleReader::Open("ntuple", fileGuard.GetPath());

   auto inspector = RNTupleInspector::Create(ntuple).Unwrap();
   EXPECT_EQ(sizeof(int32_t), inspector->GetUncompressedSize());
   EXPECT_EQ(inspector->GetCompressedSize(), inspector->GetUncompressedSize());
}

TEST(RNTupleInspector, SizeCompressed)
{
   FileRaii fileGuard("test_ntuple_inspector_size_uncompressed.root");
   {
      auto model = RNTupleModel::Create();
      auto nFldInt = model->MakeField<std::int32_t>("i");

      auto writeOptions = RNTupleWriteOptions();
      writeOptions.SetCompression(505);
      auto ntuple = RNTupleWriter::Recreate(std::move(model), "ntuple", fileGuard.GetPath(), writeOptions);

      for (int32_t i = 0; i < 50; ++i) {
         *nFldInt = i;
         ntuple->Fill();
      }
   }

   auto ntuple = RNTupleReader::Open("ntuple", fileGuard.GetPath());

   auto inspector = RNTupleInspector::Create(ntuple).Unwrap();
   EXPECT_NE(inspector->GetCompressedSize(), inspector->GetUncompressedSize());
}

TEST(RNTupleInspector, SizeEmpty)
{
   FileRaii fileGuard("test_ntuple_inspector_size_empty.root");
   {
      auto model = RNTupleModel::Create();
      RNTupleWriter::Recreate(std::move(model), "ntuple", fileGuard.GetPath());
   }

   auto ntuple = RNTupleReader::Open("ntuple", fileGuard.GetPath());

   auto inspector = RNTupleInspector::Create(ntuple).Unwrap();
   EXPECT_EQ(0, inspector->GetCompressedSize());
   EXPECT_EQ(0, inspector->GetUncompressedSize());
}

TEST(RNTupleInspector, SingleIntFieldCompression)
{
   FileRaii fileGuard("test_ntuple_inspector_size_single_int_field.root");
   {
      auto model = RNTupleModel::Create();
      auto nFldInt = model->MakeField<std::int32_t>("i");

      auto writeOptions = RNTupleWriteOptions();
      writeOptions.SetCompression(505);
      auto ntuple = RNTupleWriter::Recreate(std::move(model), "ntuple", fileGuard.GetPath(), writeOptions);

      for (int32_t i = 0; i < 50; ++i) {
         *nFldInt = i;
         ntuple->Fill();
      }
   }

   auto ntuple = RNTupleReader::Open("ntuple", fileGuard.GetPath());

   auto inspector = RNTupleInspector::Create(ntuple).Unwrap();
   EXPECT_EQ(200, inspector->GetUncompressedSize());
   EXPECT_EQ(95, inspector->GetCompressedSize());
   EXPECT_NEAR(2.11, inspector->GetCompressionFactor(), 1e-2);
}
